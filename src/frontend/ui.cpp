#include "ui.hpp"

#include "fk1/machine.hpp"

#include <SDL3/SDL.h>

#include "imgui.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fk1::frontend {
namespace {

enum class DiskDialogOperation {
    open,
    create_blank,
};

struct DiskDialogContext {
    std::size_t drive{0};
    FloppyAccess access{FloppyAccess::read_only};
    DiskDialogOperation operation{DiskDialogOperation::open};
    std::string default_location;
};

struct DiskDialogResult {
    std::size_t drive{0};
    FloppyAccess access{FloppyAccess::read_only};
    DiskDialogOperation operation{DiskDialogOperation::open};
    std::optional<std::string> path;
    std::string error;
};

std::mutex file_dialog_mutex;
std::vector<DiskDialogResult> file_dialog_results;

void SDLCALL file_dialog_callback(
    void* userdata,
    const char* const* file_list,
    int)
{
    const auto context = std::unique_ptr<DiskDialogContext>(
        static_cast<DiskDialogContext*>(userdata));
    DiskDialogResult result{
        context->drive,
        context->access,
        context->operation,
        std::nullopt,
        {},
    };
    if(file_list == nullptr) {
        result.error = SDL_GetError();
    } else if(*file_list != nullptr) {
        result.path = *file_list;
    }

    const std::scoped_lock lock(file_dialog_mutex);
    file_dialog_results.push_back(std::move(result));
}

std::filesystem::path path_from_utf8(const std::string_view text)
{
    const auto* begin = reinterpret_cast<const char8_t*>(text.data());
    return std::filesystem::path{std::u8string{begin, begin + text.size()}};
}

std::string path_to_utf8(const std::filesystem::path& path)
{
    const auto text = path.u8string();
    return std::string{
        reinterpret_cast<const char*>(text.data()),
        text.size(),
    };
}

bool same_existing_file(
    const std::filesystem::path& left,
    const std::filesystem::path& right) noexcept
{
    std::error_code error;
    return std::filesystem::equivalent(left, right, error) && !error;
}

char drive_name(const std::size_t drive) noexcept
{
    return drive == FloppySubsystem::drive_a ? 'A' : 'B';
}

void finish_disk_dialog(bool& paused, UiState& state)
{
    if(state.disk_dialog_was_running) {
        paused = false;
    }
    state.disk_dialog_was_running = false;
}

void process_file_dialog_results(
    Machine& machine,
    bool& paused,
    std::array<MountedDisk, 2>& disks,
    UiState& state)
{
    std::vector<DiskDialogResult> results;
    {
        const std::scoped_lock lock(file_dialog_mutex);
        results.swap(file_dialog_results);
    }

    for(auto& result : results) {
        state.native_file_dialog_open = false;
        if(!result.error.empty()) {
            state.status = "File dialog failed: " + result.error;
        } else if(result.path) {
            std::string error;
            auto path = path_from_utf8(*result.path);
            if(result.operation == DiskDialogOperation::create_blank
               && path.extension().empty()) {
                path += ".8sd";
            }
            if(result.drive >= disks.size()) {
                state.status = "Disk open failed: invalid drive";
            } else if(std::any_of(
                          state.protected_disk_paths.begin(),
                          state.protected_disk_paths.end(),
                          [&path](const auto& protected_path) {
                              return same_existing_file(path, protected_path);
                          })) {
                state.status = "Disk open failed: file is in use by the ROM or printer";
            } else {
                const auto other_drive = result.drive == FloppySubsystem::drive_a
                    ? FloppySubsystem::drive_b
                    : FloppySubsystem::drive_a;
                const auto shared_writable_image = disks[other_drive].path
                    && same_existing_file(path, *disks[other_drive].path)
                    && (result.access == FloppyAccess::read_write
                        || disks[other_drive].access == FloppyAccess::read_write);
                if(shared_writable_image) {
                    state.status =
                        "Disk open failed: a writable image cannot be shared by both drives";
                } else {
                    const auto creating_blank =
                        result.operation == DiskDialogOperation::create_blank;
                    if(creating_blank && !create_blank_8sd(path, error)) {
                        state.status = "Blank disk creation failed: " + error;
                    } else if(replace_8sd(
                                  machine,
                                  result.drive,
                                  path,
                                  result.access,
                                  disks[result.drive],
                                  error)) {
                        state.disk_dialog_paths[result.drive] = path_to_utf8(path);
                        state.open_writable[result.drive] =
                            result.access == FloppyAccess::read_write;
                        state.status = "Drive ";
                        state.status += drive_name(result.drive);
                        state.status += creating_blank
                            ? ": blank disk created and mounted read-write"
                            : (result.access == FloppyAccess::read_write
                                   ? ": disk opened read-write"
                                   : ": disk opened read-only");
                    } else {
                        state.status = creating_blank
                            ? "Blank disk was created but could not be mounted: " + error
                            : "Disk open failed: " + error;
                    }
                }
            }
        } else {
            state.status = "Disk selection cancelled";
        }
        finish_disk_dialog(paused, state);
    }
}

void show_disk_dialog(
    SDL_Window* window,
    const std::size_t drive,
    const FloppyAccess access,
    const DiskDialogOperation operation,
    bool& paused,
    UiState& state)
{
    if(state.native_file_dialog_open) {
        return;
    }

    static constexpr std::array filters{
        SDL_DialogFileFilter{"FK-1 disk images", "8sd"},
        SDL_DialogFileFilter{"All files", "*"},
    };

    state.disk_dialog_was_running = !paused;
    paused = true;
    state.native_file_dialog_open = true;
    state.open_writable[drive] = access == FloppyAccess::read_write;
    state.status = operation == DiskDialogOperation::create_blank
        ? "Choose a path for a new blank disk in drive "
        : "Select an image for drive ";
    state.status += drive_name(drive);

    auto context = std::make_unique<DiskDialogContext>();
    context->drive = drive;
    context->access = access;
    context->operation = operation;
    context->default_location = state.disk_dialog_paths[drive];
    if(operation == DiskDialogOperation::create_blank
       && !context->default_location.empty()) {
        context->default_location = path_to_utf8(
            path_from_utf8(context->default_location).parent_path());
    }
    auto* callback_context = context.release();
    const auto* default_location = callback_context->default_location.empty()
        ? nullptr
        : callback_context->default_location.c_str();
    if(operation == DiskDialogOperation::create_blank) {
        SDL_ShowSaveFileDialog(
            file_dialog_callback,
            callback_context,
            window,
            filters.data(),
            static_cast<int>(filters.size()),
            default_location);
    } else {
        SDL_ShowOpenFileDialog(
            file_dialog_callback,
            callback_context,
            window,
            filters.data(),
            static_cast<int>(filters.size()),
            default_location,
            false);
    }
}

void eject_disk(
    Machine& machine,
    const std::size_t drive,
    std::array<MountedDisk, 2>& disks,
    UiState& state)
{
    std::string error;
    if(eject_8sd(machine, drive, disks[drive], error)) {
        state.status = "Drive ";
        state.status += drive_name(drive);
        state.status += ": disk ejected";
    } else {
        state.status = "Disk eject failed: " + error;
    }
}

void reset_machine(Machine& machine, bool& paused, UiState& state)
{
    machine.reset();
    paused = false;
    state.status = "Machine reset";
}

void toggle_pause(bool& paused, UiState& state)
{
    paused = !paused;
    state.status = paused ? "Emulation paused" : "Emulation running";
}

void toggle_fullscreen(SDL_Window* window, UiState& state)
{
    const auto requested = !state.fullscreen;
    if(SDL_SetWindowFullscreen(window, requested)) {
        state.fullscreen = requested;
        state.status = requested ? "Fullscreen enabled" : "Fullscreen disabled";
    } else {
        state.status = std::string{"Fullscreen failed: "} + SDL_GetError();
    }
}

void draw_drive_menu(
    SDL_Window* window,
    Machine& machine,
    const std::size_t drive,
    bool& paused,
    std::array<MountedDisk, 2>& disks,
    UiState& state)
{
    std::string label{"Drive "};
    label += drive_name(drive);
    if(!ImGui::BeginMenu(label.c_str())) {
        return;
    }
    if(ImGui::MenuItem("Create blank image...")) {
        show_disk_dialog(
            window,
            drive,
            FloppyAccess::read_write,
            DiskDialogOperation::create_blank,
            paused,
            state);
    }
    ImGui::Separator();
    if(ImGui::MenuItem("Open read-only...")) {
        show_disk_dialog(
            window,
            drive,
            FloppyAccess::read_only,
            DiskDialogOperation::open,
            paused,
            state);
    }
    if(ImGui::MenuItem("Open read-write...")) {
        show_disk_dialog(
            window,
            drive,
            FloppyAccess::read_write,
            DiskDialogOperation::open,
            paused,
            state);
    }
    ImGui::BeginDisabled(!disks[drive].path.has_value());
    if(ImGui::MenuItem("Eject")) {
        eject_disk(machine, drive, disks, state);
    }
    ImGui::EndDisabled();
    ImGui::EndMenu();
}

void draw_menu_bar(
    SDL_Window* window,
    Machine& machine,
    bool& paused,
    const bool mouse_captured,
    std::array<MountedDisk, 2>& disks,
    UiState& state)
{
    if(!ImGui::BeginMainMenuBar()) {
        return;
    }

    if(ImGui::BeginMenu("File")) {
        draw_drive_menu(
            window, machine, FloppySubsystem::drive_a, paused, disks, state);
        draw_drive_menu(
            window, machine, FloppySubsystem::drive_b, paused, disks, state);
        ImGui::Separator();
        if(ImGui::MenuItem("Exit")) {
            state.quit_requested = true;
        }
        ImGui::EndMenu();
    }
    if(ImGui::BeginMenu("Control")) {
        if(ImGui::MenuItem("Reset")) {
            reset_machine(machine, paused, state);
        }
        if(ImGui::MenuItem(paused ? "Resume" : "Pause", "F5")) {
            toggle_pause(paused, state);
        }
        if(ImGui::MenuItem(
               mouse_captured ? "Release mouse" : "Capture mouse",
               "F12")) {
            state.mouse_capture_request = !mouse_captured;
        }
        ImGui::EndMenu();
    }
    if(ImGui::BeginMenu("View")) {
        if(ImGui::MenuItem("Fullscreen", nullptr, state.fullscreen)) {
            toggle_fullscreen(window, state);
        }
        ImGui::MenuItem("Scanlines", nullptr, &state.scanlines);
        ImGui::EndMenu();
    }
    if(ImGui::BeginMenu("Help")) {
        if(ImGui::MenuItem("About FK-1")) {
            state.show_about = true;
        }
        ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
}

void draw_toolbar(
    Machine& machine,
    bool& paused,
    const bool mouse_captured,
    UiState& state)
{
    if(ImGui::Button("Reset")) {
        reset_machine(machine, paused, state);
    }
    ImGui::SameLine();
    if(ImGui::Button(paused ? "Resume" : "Pause")) {
        toggle_pause(paused, state);
    }
    ImGui::SameLine();
    if(ImGui::Button(mouse_captured ? "Release mouse" : "Capture mouse")) {
        state.mouse_capture_request = !mouse_captured;
    }
}

void draw_drive_row(
    SDL_Window* window,
    Machine& machine,
    const std::size_t drive,
    bool& paused,
    std::array<MountedDisk, 2>& disks,
    UiState& state)
{
    ImGui::PushID(static_cast<int>(drive));
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Drive %c", drive_name(drive));
    ImGui::SameLine();
    if(ImGui::Button("New...")) {
        show_disk_dialog(
            window,
            drive,
            FloppyAccess::read_write,
            DiskDialogOperation::create_blank,
            paused,
            state);
    }
    ImGui::SameLine();
    if(ImGui::Button("Open...")) {
        show_disk_dialog(
            window,
            drive,
            state.open_writable[drive]
                ? FloppyAccess::read_write
                : FloppyAccess::read_only,
            DiskDialogOperation::open,
            paused,
            state);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!disks[drive].path.has_value());
    if(ImGui::Button("Eject")) {
        eject_disk(machine, drive, disks, state);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::Checkbox("Writable", &state.open_writable[drive]);
    ImGui::SameLine();

    if(disks[drive].path) {
        const auto filename = path_to_utf8(disks[drive].path->filename());
        const auto full_path = path_to_utf8(*disks[drive].path);
        const auto& physical_drive = machine.floppy().drive(drive);
        ImGui::TextDisabled(
            "%s  [%s, track %u]",
            filename.c_str(),
            disks[drive].access == FloppyAccess::read_write ? "RW" : "RO",
            static_cast<unsigned>(physical_drive.head_track()));
        if(ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", full_path.c_str());
        }
    } else {
        ImGui::TextDisabled("(empty)");
    }
    ImGui::PopID();
}

void draw_screen(SDL_Texture* texture, UiState& state)
{
    const auto available = ImGui::GetContentRegionAvail();
    // The original display uses non-square pixels; preserve its 4:3 physical
    // aspect ratio independently of the 512x256 framebuffer resolution.
    constexpr auto aspect = 4.0F / 3.0F;
    ImVec2 size{available.x, available.x / aspect};
    if(size.y > available.y) {
        size = {available.y * aspect, available.y};
    }
    size.x = std::max(size.x, 1.0F);
    size.y = std::max(size.y, 1.0F);

    const auto cursor = ImGui::GetCursorPos();
    ImGui::SetCursorPosX(cursor.x + std::max(0.0F, (available.x - size.x) * 0.5F));
    ImGui::SetCursorPosY(cursor.y + std::max(0.0F, (available.y - size.y) * 0.5F));
    const auto top_left = ImGui::GetCursorScreenPos();
    ImGui::Image(reinterpret_cast<ImTextureID>(texture), size);
    state.screen_hovered = ImGui::IsItemHovered();

    if(!state.scanlines) {
        return;
    }
    auto* draw_list = ImGui::GetWindowDrawList();
    const auto row_height = size.y / static_cast<float>(Machine::video_height);
    for(std::size_t row = 1; row < Machine::video_height; row += 2) {
        const auto y = top_left.y + static_cast<float>(row) * row_height;
        draw_list->AddRectFilled(
            {top_left.x, y},
            {top_left.x + size.x, y + row_height},
            IM_COL32(0, 0, 0, 70));
    }
}

void draw_status_bar(
    const Machine& machine,
    const bool paused,
    const bool mouse_captured,
    const std::array<MountedDisk, 2>& disks,
    const UiState& state)
{
    const auto cursor = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddCircleFilled(
        {cursor.x + 7.0F, cursor.y + 9.0F},
        5.0F,
        paused ? IM_COL32(230, 180, 30, 255) : IM_COL32(40, 210, 90, 255));
    ImGui::Dummy({16.0F, 1.0F});
    ImGui::SameLine();
    ImGui::TextUnformatted(paused ? "PAUSED" : "RUNNING");
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::Text(
        "A: %s | B: %s | SERIAL: %s | PRINTER: %s | MOUSE: %s | %s",
        disks[0].path ? "mounted" : "empty",
        disks[1].path ? "mounted" : "empty",
        state.serial_enabled
            ? (machine.serial_connected() ? "connected" : "offline")
            : "disabled",
        machine.printer_online() ? "online" : "offline",
        mouse_captured ? "captured" : "free",
        state.status.c_str());
}

void draw_main_window(
    SDL_Window* window,
    SDL_Texture* texture,
    Machine& machine,
    bool& paused,
    const bool mouse_captured,
    std::array<MountedDisk, 2>& disks,
    UiState& state)
{
    const auto* viewport = ImGui::GetMainViewport();
    const auto menu_height = ImGui::GetFrameHeight();
    ImGui::SetNextWindowPos({viewport->Pos.x, viewport->Pos.y + menu_height});
    ImGui::SetNextWindowSize(
        {viewport->Size.x, std::max(1.0F, viewport->Size.y - menu_height)});
    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("##FK1Main", nullptr, flags);

    draw_toolbar(machine, paused, mouse_captured, state);
    draw_drive_row(
        window, machine, FloppySubsystem::drive_a, paused, disks, state);
    draw_drive_row(
        window, machine, FloppySubsystem::drive_b, paused, disks, state);
    ImGui::Separator();

    const auto status_height = ImGui::GetFrameHeightWithSpacing();
    ImGui::BeginChild(
        "##EmulatorScreen",
        {0.0F, -status_height},
        ImGuiChildFlags_Borders);
    draw_screen(texture, state);
    ImGui::EndChild();
    draw_status_bar(machine, paused, mouse_captured, disks, state);
    ImGui::End();
}

void draw_about(UiState& state)
{
    if(!state.show_about) {
        return;
    }
    ImGui::SetNextWindowSize({400.0F, 0.0F}, ImGuiCond_FirstUseEver);
    if(ImGui::Begin(
           "About FK-1",
           &state.show_about,
           ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("FK-1 Emulator C++/SDL3");
        ImGui::Separator();
        ImGui::TextWrapped(
            "Emulator of the Czechoslovak FK-1 computer. The user interface "
            "uses Dear ImGui and SDL3.");
        ImGui::TextUnformatted("F12 captures or releases the emulated mouse.");
    }
    ImGui::End();
}

} // namespace

void draw_ui(
    SDL_Window* window,
    SDL_Texture* screen_texture,
    Machine& machine,
    bool& paused,
    const bool mouse_captured,
    std::array<MountedDisk, 2>& disks,
    UiState& state)
{
    process_file_dialog_results(machine, paused, disks, state);
    draw_menu_bar(window, machine, paused, mouse_captured, disks, state);
    draw_main_window(
        window,
        screen_texture,
        machine,
        paused,
        mouse_captured,
        disks,
        state);
    draw_about(state);
}

} // namespace fk1::frontend
