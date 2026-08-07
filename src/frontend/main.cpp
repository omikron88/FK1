#include "fk1/machine.hpp"
#include "fk1/keyboard.hpp"
#include "disk_files.hpp"
#include "serial_tcp.hpp"
#include "ui.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

#include <array>
#include <chrono>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

namespace {

void print_sdl_error(const char* operation)
{
    std::cerr << operation << ": " << SDL_GetError() << '\n';
}

struct Options {
    std::optional<std::filesystem::path> rom_path;
    std::optional<std::filesystem::path> printer_path;
    std::optional<fk1::frontend::TcpEndpoint> serial_endpoint;
    fk1::frontend::TcpSerialMode serial_mode{fk1::frontend::TcpSerialMode::listen};
    fk1::frontend::MountedDisk disk_a;
    fk1::frontend::MountedDisk disk_b;
    std::uint32_t index_pulse_us{1'700};
    bool show_help{false};
    bool show_rom_info{false};
};

void print_usage(std::ostream& stream, const char* executable)
{
    stream << "Usage: " << executable
           << " [--rom <path>] [--disk-a <path>] [--disk-b <path>]"
              " [--disk-a-rw <path>] [--disk-b-rw <path>] [--index-us <value>]"
              " [--printer <path>] [--serial-listen <host:port>]"
              " [--serial-connect <host:port>] [--rom-info] [--help]\n"
           << "  --rom <path>      Load an alternative 2, 4, 8 or 16 KiB ROM image.\n"
           << "  --disk-a <path>   Mount a 77x26x128-byte .8sd image read-only in drive A.\n"
           << "  --disk-b <path>   Mount a 77x26x128-byte .8sd image read-only in drive B.\n"
           << "  --disk-a-rw <p>   Mount drive A read-write and save valid sectors on exit.\n"
           << "  --disk-b-rw <p>   Mount drive B read-write and save valid sectors on exit.\n"
           << "  --index-us <n>    Set Index pulse width in microseconds (default: 1700).\n"
           << "  --printer <path>  Append raw Centronics output to a file; otherwise offline.\n"
           << "  --serial-listen <host:port>   Listen for one raw TCP serial peer.\n"
           << "  --serial-connect <host:port>  Connect raw TCP serial; retry after loss.\n"
           << "  --rom-info        Print the selected ROM source and size, then exit.\n"
           << "  --help            Show this help.\n";
}

bool parse_path_option(
    const int argc,
    char** argv,
    int& index,
    const std::string_view name,
    std::optional<std::filesystem::path>& destination,
    std::string& error)
{
    if(index + 1 >= argc) {
        error = std::string{name} + " requires a file path";
        return false;
    }
    destination = std::filesystem::path{argv[++index]};
    return true;
}

bool parse_index_width(const std::string_view text, std::uint32_t& value) noexcept
{
    std::uint32_t parsed = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if(result.ec != std::errc{} || result.ptr != text.data() + text.size()
       || static_cast<std::uint64_t>(parsed) * 12U >= fk1::FloppyDrive::revolution_ticks) {
        return false;
    }
    value = parsed;
    return true;
}

bool parse_disk_option(
    const int argc,
    char** argv,
    int& index,
    const std::string_view name,
    fk1::frontend::MountedDisk& destination,
    const fk1::FloppyAccess access,
    std::string& error)
{
    if(!parse_path_option(argc, argv, index, name, destination.path, error)) {
        return false;
    }
    destination.access = access;
    return true;
}

bool parse_disk_option_value(
    const std::string_view value,
    const std::string_view name,
    fk1::frontend::MountedDisk& destination,
    const fk1::FloppyAccess access,
    std::string& error)
{
    if(value.empty()) {
        error = std::string{name} + " requires a file path";
        return false;
    }
    destination.path = std::filesystem::path{value};
    destination.access = access;
    return true;
}

bool parse_serial_option(
    const std::string_view value,
    const std::string_view name,
    const fk1::frontend::TcpSerialMode mode,
    Options& options,
    std::string& error)
{
    if(options.serial_endpoint) {
        error = "--serial-listen and --serial-connect are mutually exclusive";
        return false;
    }
    auto endpoint = fk1::frontend::parse_tcp_endpoint(value, error);
    if(!endpoint) {
        error = std::string{name} + ": " + error;
        return false;
    }
    options.serial_endpoint = std::move(*endpoint);
    options.serial_mode = mode;
    return true;
}

bool parse_options(const int argc, char** argv, Options& options, std::string& error)
{
    for(int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};

        if(argument == "--help" || argument == "-h") {
            options.show_help = true;
        } else if(argument == "--rom-info") {
            options.show_rom_info = true;
        } else if(argument == "--rom") {
            if(!parse_path_option(argc, argv, index, argument, options.rom_path, error)) {
                return false;
            }
        } else if(argument == "--printer") {
            if(!parse_path_option(argc, argv, index, argument, options.printer_path, error)) {
                return false;
            }
        } else if(argument == "--serial-listen" || argument == "--serial-connect") {
            if(index + 1 >= argc) {
                error = std::string{argument} + " requires host:port";
                return false;
            }
            const auto mode = argument == "--serial-listen"
                ? fk1::frontend::TcpSerialMode::listen
                : fk1::frontend::TcpSerialMode::connect;
            if(!parse_serial_option(argv[++index], argument, mode, options, error)) {
                return false;
            }
        } else if(argument == "--disk-a") {
            if(!parse_disk_option(
                   argc, argv, index, argument, options.disk_a,
                   fk1::FloppyAccess::read_only, error)) {
                return false;
            }
        } else if(argument == "--disk-b") {
            if(!parse_disk_option(
                   argc, argv, index, argument, options.disk_b,
                   fk1::FloppyAccess::read_only, error)) {
                return false;
            }
        } else if(argument == "--disk-a-rw") {
            if(!parse_disk_option(
                   argc, argv, index, argument, options.disk_a,
                   fk1::FloppyAccess::read_write, error)) {
                return false;
            }
        } else if(argument == "--disk-b-rw") {
            if(!parse_disk_option(
                   argc, argv, index, argument, options.disk_b,
                   fk1::FloppyAccess::read_write, error)) {
                return false;
            }
        } else if(argument == "--index-us") {
            if(index + 1 >= argc || !parse_index_width(argv[++index], options.index_pulse_us)) {
                error = "--index-us requires an integer from 0 through 166666";
                return false;
            }
        } else if(argument.starts_with("--rom=")) {
            const auto value = argument.substr(std::string_view{"--rom="}.size());
            if(value.empty()) {
                error = "--rom requires a file path";
                return false;
            }
            options.rom_path = std::filesystem::path{value};
        } else if(argument.starts_with("--printer=")) {
            const auto value = argument.substr(std::string_view{"--printer="}.size());
            if(value.empty()) {
                error = "--printer requires a file path";
                return false;
            }
            options.printer_path = std::filesystem::path{value};
        } else if(argument.starts_with("--serial-listen=")) {
            const auto value = argument.substr(std::string_view{"--serial-listen="}.size());
            if(!parse_serial_option(
                   value,
                   "--serial-listen",
                   fk1::frontend::TcpSerialMode::listen,
                   options,
                   error)) {
                return false;
            }
        } else if(argument.starts_with("--serial-connect=")) {
            const auto value = argument.substr(std::string_view{"--serial-connect="}.size());
            if(!parse_serial_option(
                   value,
                   "--serial-connect",
                   fk1::frontend::TcpSerialMode::connect,
                   options,
                   error)) {
                return false;
            }
        } else if(argument.starts_with("--disk-a=")) {
            const auto value = argument.substr(std::string_view{"--disk-a="}.size());
            if(!parse_disk_option_value(
                   value, "--disk-a", options.disk_a,
                   fk1::FloppyAccess::read_only, error)) {
                return false;
            }
        } else if(argument.starts_with("--disk-b=")) {
            const auto value = argument.substr(std::string_view{"--disk-b="}.size());
            if(!parse_disk_option_value(
                   value, "--disk-b", options.disk_b,
                   fk1::FloppyAccess::read_only, error)) {
                return false;
            }
        } else if(argument.starts_with("--disk-a-rw=")) {
            const auto value = argument.substr(std::string_view{"--disk-a-rw="}.size());
            if(!parse_disk_option_value(
                   value, "--disk-a-rw", options.disk_a,
                   fk1::FloppyAccess::read_write, error)) {
                return false;
            }
        } else if(argument.starts_with("--disk-b-rw=")) {
            const auto value = argument.substr(std::string_view{"--disk-b-rw="}.size());
            if(!parse_disk_option_value(
                   value, "--disk-b-rw", options.disk_b,
                   fk1::FloppyAccess::read_write, error)) {
                return false;
            }
        } else if(argument.starts_with("--index-us=")) {
            const auto value = argument.substr(std::string_view{"--index-us="}.size());
            if(!parse_index_width(value, options.index_pulse_us)) {
                error = "--index-us requires an integer from 0 through 166666";
                return false;
            }
        } else {
            error = "unknown argument: " + std::string{argument};
            return false;
        }
    }

    return true;
}

bool same_existing_file(
    const std::filesystem::path& left,
    const std::filesystem::path& right) noexcept
{
    std::error_code error;
    const auto equivalent = std::filesystem::equivalent(left, right, error);
    return !error && equivalent;
}

std::optional<std::vector<std::uint8_t>> read_rom(
    const std::filesystem::path& path,
    std::string& error)
{
    std::ifstream stream{path, std::ios::binary | std::ios::ate};
    if(!stream) {
        error = "cannot open ROM file: " + path.string();
        return std::nullopt;
    }

    const auto end = stream.tellg();
    if(end <= 0) {
        error = "ROM file is empty or unreadable: " + path.string();
        return std::nullopt;
    }

    const auto size = static_cast<std::uintmax_t>(end);
    if(!fk1::Machine::is_supported_rom_size(static_cast<std::size_t>(size))) {
        error = "ROM file must contain exactly 2, 4, 8 or 16 KiB: " + path.string();
        return std::nullopt;
    }

    std::vector<std::uint8_t> image(static_cast<std::size_t>(size));
    stream.seekg(0, std::ios::beg);
    if(!stream.read(
           reinterpret_cast<char*>(image.data()),
           static_cast<std::streamsize>(image.size()))) {
        error = "cannot read ROM file: " + path.string();
        return std::nullopt;
    }

    return image;
}

std::optional<std::uint8_t> translate_key(const SDL_KeyboardEvent& event)
{
    const auto shift = (event.mod & SDL_KMOD_SHIFT) != 0;
    using SpecialKey = fk1::Keyboard::SpecialKey;

    switch(event.key) {
    case SDLK_DELETE:
        return fk1::Keyboard::special_code(SpecialKey::roll, shift);
    case SDLK_INSERT:
        return fk1::Keyboard::special_code(SpecialKey::copy, shift);
    case SDLK_END:
        return fk1::Keyboard::special_code(SpecialKey::break_key, shift);
    case SDLK_UP:
        return fk1::Keyboard::special_code(SpecialKey::up);
    case SDLK_DOWN:
        return fk1::Keyboard::special_code(SpecialKey::down);
    case SDLK_RIGHT:
        return fk1::Keyboard::special_code(SpecialKey::right);
    case SDLK_LEFT:
        return fk1::Keyboard::special_code(SpecialKey::left);
    case SDLK_HOME:
        return fk1::Keyboard::special_code(SpecialKey::home);
    case SDLK_F1:
        return fk1::Keyboard::special_code(SpecialKey::user_1);
    case SDLK_F2:
        return fk1::Keyboard::special_code(SpecialKey::user_2);
    case SDLK_F3:
        return fk1::Keyboard::special_code(SpecialKey::user_3);
    case SDLK_ESCAPE:
        return fk1::Keyboard::ascii_code(0x1B);
    case SDLK_BACKSPACE:
    case SDLK_KP_BACKSPACE:
        return fk1::Keyboard::ascii_code(0x08);
    case SDLK_TAB:
    case SDLK_LEFT_TAB:
    case SDLK_KP_TAB:
        return fk1::Keyboard::ascii_code(0x09);
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
    case SDLK_RETURN2:
        return fk1::Keyboard::ascii_code(0x0D);
    default:
        break;
    }

    const auto layout_modifiers = static_cast<SDL_Keymod>(
        event.mod & static_cast<SDL_Keymod>(SDL_KMOD_SHIFT | SDL_KMOD_CAPS));
    const auto character = SDL_GetKeyFromScancode(event.scancode, layout_modifiers, false);
    const auto control = (event.mod & SDL_KMOD_CTRL) != 0
        && (event.mod & SDL_KMOD_ALT) == 0;
    return fk1::Keyboard::ascii_code(static_cast<std::uint32_t>(character), control);
}

} // namespace

int main(const int argc, char** argv)
{
    Options options;
    std::string option_error;
    if(!parse_options(argc, argv, options, option_error)) {
        std::cerr << "Error: " << option_error << "\n\n";
        print_usage(std::cerr, argv[0]);
        return EXIT_FAILURE;
    }

    if(options.show_help) {
        print_usage(std::cout, argv[0]);
        return EXIT_SUCCESS;
    }

    if(options.printer_path
       && ((options.rom_path && same_existing_file(*options.printer_path, *options.rom_path))
           || (options.disk_a.path
               && same_existing_file(*options.printer_path, *options.disk_a.path))
           || (options.disk_b.path
               && same_existing_file(*options.printer_path, *options.disk_b.path)))) {
        std::cerr << "Error: printer output must not use a mounted disk or ROM file\n";
        return EXIT_FAILURE;
    }

    fk1::Machine machine;
    std::string rom_source = "embedded boot ROM";
    if(options.rom_path) {
        std::string rom_error;
        auto image = read_rom(*options.rom_path, rom_error);
        if(!image || !machine.set_rom_image(std::span<const std::uint8_t>{*image})) {
            std::cerr << "Error: " << (rom_error.empty() ? "invalid ROM image" : rom_error) << '\n';
            return EXIT_FAILURE;
        }
        rom_source = options.rom_path->string();
    }

    if(options.show_rom_info) {
        std::cout << rom_source << ": " << machine.rom_image().size() << " bytes\n";
        return EXIT_SUCCESS;
    }

    std::cout << "Using " << rom_source << " (" << machine.rom_image().size() << " bytes)\n";

    if(!machine.set_floppy_index_pulse_width(options.index_pulse_us * 12U)) {
        std::cerr << "Error: invalid Index pulse width\n";
        return EXIT_FAILURE;
    }
    std::array<fk1::frontend::MountedDisk, 2> mounted_disks{};
    for(const auto& [disk, drive, name] : std::array{
            std::tuple{options.disk_a, fk1::FloppySubsystem::drive_a, 'A'},
            std::tuple{options.disk_b, fk1::FloppySubsystem::drive_b, 'B'},
        }) {
        if(disk.path) {
            std::string disk_error;
            if(!fk1::frontend::replace_8sd(
                   machine,
                   drive,
                   *disk.path,
                   disk.access,
                   mounted_disks[drive],
                   disk_error)) {
                std::cerr << "Error: " << disk_error << '\n';
                return EXIT_FAILURE;
            }
            std::cout << "Mounted drive " << name << ": " << disk.path->string()
                      << (disk.access == fk1::FloppyAccess::read_write
                              ? " (read-write IBM 3740 FM)\n"
                              : " (read-only IBM 3740 FM)\n");
        }
    }

    std::ofstream printer_stream;
    if(options.printer_path) {
        printer_stream.open(*options.printer_path, std::ios::binary | std::ios::app);
        if(!printer_stream) {
            std::cerr << "Error: cannot open printer output file: "
                      << options.printer_path->string() << '\n';
            return EXIT_FAILURE;
        }
        machine.set_printer_online(true);
        std::cout << "Printer online, appending raw output to: "
                  << options.printer_path->string() << '\n';
    }

    fk1::frontend::TcpSerialLink serial_link;
    const auto serial_enabled = options.serial_endpoint.has_value();
    if(options.serial_endpoint) {
        std::string serial_error;
        if(!serial_link.start(options.serial_mode, *options.serial_endpoint, serial_error)) {
            std::cerr << "Error: " << serial_error << '\n';
            return EXIT_FAILURE;
        }
        // Once a transport was requested, an absent TCP peer represents an
        // unplugged cable instead of the diagnostic-friendly standalone state.
        machine.set_serial_connected(serial_link.connected());
    }

    if(!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        print_sdl_error("SDL_Init failed");
        return EXIT_FAILURE;
    }

    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    if(!SDL_CreateWindowAndRenderer(
           "FK-1 Emulator",
           960,
           720,
           SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY,
           &window,
           &renderer)) {
        print_sdl_error("SDL_CreateWindowAndRenderer failed");
        SDL_Quit();
        return EXIT_FAILURE;
    }

    SDL_Texture* texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        static_cast<int>(fk1::Machine::video_width),
        static_cast<int>(fk1::Machine::video_height));

    if(texture == nullptr) {
        print_sdl_error("SDL_CreateTexture failed");
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return EXIT_FAILURE;
    }

    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto& imgui_io = ImGui::GetIO();
    imgui_io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    imgui_io.ConfigNavCaptureKeyboard = false;
    ImGui::StyleColorsDark();
    if(!ImGui_ImplSDL3_InitForSDLRenderer(window, renderer)
       || !ImGui_ImplSDLRenderer3_Init(renderer)) {
        std::cerr << "Error: cannot initialize Dear ImGui SDL3 backend\n";
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return EXIT_FAILURE;
    }

    bool running = true;
    bool paused = false;
    bool printer_write_failed = false;
    bool mouse_captured = false;
    fk1::frontend::UiState ui_state;
    ui_state.serial_enabled = serial_enabled;
    if(options.rom_path) {
        ui_state.protected_disk_paths.push_back(*options.rom_path);
    }
    if(options.printer_path) {
        ui_state.protected_disk_paths.push_back(*options.printer_path);
    }
    for(std::size_t drive = 0; drive < mounted_disks.size(); ++drive) {
        if(mounted_disks[drive].path) {
            ui_state.disk_dialog_paths[drive] = mounted_disks[drive].path->string();
            ui_state.open_writable[drive] =
                mounted_disks[drive].access == fk1::FloppyAccess::read_write;
        }
    }
    std::optional<std::uint8_t> pending_serial_input;
    std::optional<std::uint8_t> pending_serial_output;
    auto next_frame = std::chrono::steady_clock::now();
    constexpr auto host_frame_period = std::chrono::milliseconds(20);

    const auto synchronize_serial = [&]() {
        if(!serial_enabled) {
            return;
        }

        serial_link.poll();
        while(auto message = serial_link.take_status_message()) {
            std::cout << *message << '\n';
            ui_state.status = *message;
        }

        const auto connected = serial_link.connected();
        if(machine.serial_connected() != connected) {
            machine.set_serial_connected(connected);
            if(!connected) {
                pending_serial_input.reset();
                pending_serial_output.reset();
            }
        }
        if(!connected) {
            return;
        }

        if(pending_serial_input
           && machine.queue_serial_received_byte(*pending_serial_input)) {
            pending_serial_input.reset();
        }
        while(!pending_serial_input) {
            const auto byte = serial_link.take_received_byte();
            if(!byte) {
                break;
            }
            if(!machine.queue_serial_received_byte(*byte)) {
                pending_serial_input = *byte;
            }
        }

        if(pending_serial_output && serial_link.queue_transmit(*pending_serial_output)) {
            pending_serial_output.reset();
        }
        while(!pending_serial_output) {
            const auto byte = machine.take_serial_transmitted_byte();
            if(!byte) {
                break;
            }
            if(!serial_link.queue_transmit(*byte)) {
                pending_serial_output = *byte;
            }
        }
        machine.set_serial_clear_to_send(!pending_serial_output.has_value());

        // Give newly queued output to the non-blocking socket immediately.
        serial_link.poll();
        while(auto message = serial_link.take_status_message()) {
            std::cout << *message << '\n';
            ui_state.status = *message;
        }
        if(machine.serial_connected() != serial_link.connected()) {
            machine.set_serial_connected(serial_link.connected());
            pending_serial_input.reset();
            pending_serial_output.reset();
        }
    };

    const auto set_mouse_capture = [&](const bool capture) {
        if(mouse_captured == capture) {
            return;
        }
        if(!SDL_SetWindowRelativeMouseMode(window, capture)) {
            print_sdl_error(capture ? "Cannot capture mouse" : "Cannot release mouse");
            return;
        }
        mouse_captured = capture;
        ui_state.status = capture ? "Mouse captured" : "Mouse released";
        if(!capture) {
            machine.set_mouse_button(fk1::MouseButton::button_1, false);
            machine.set_mouse_button(fk1::MouseButton::button_2, false);
        }
    };

    while(running) {
        SDL_Event event{};
        while(SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if(event.type == SDL_EVENT_QUIT) {
                running = false;
            } else if(event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                if(event.key.key == SDLK_F12) {
                    set_mouse_capture(!mouse_captured);
                    continue;
                }
                const auto captured = ImGui::GetIO().WantCaptureKeyboard;
                if(!captured && event.key.key == SDLK_F5) {
                    paused = !paused;
                    ui_state.status = paused ? "Emulation paused" : "Emulation running";
                    continue;
                }
                if(!captured) {
                    const auto code = translate_key(event.key);
                    if(code) {
                        machine.keyboard_key_down(
                            static_cast<std::uint32_t>(event.key.scancode),
                            *code);
                    }
                }
            } else if(event.type == SDL_EVENT_KEY_UP) {
                machine.keyboard_key_up(static_cast<std::uint32_t>(event.key.scancode));
            } else if(event.type == SDL_EVENT_MOUSE_MOTION && mouse_captured) {
                machine.mouse_motion(
                    static_cast<std::int32_t>(std::lround(event.motion.xrel)),
                    static_cast<std::int32_t>(std::lround(event.motion.yrel)));
            } else if(event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                if(!mouse_captured) {
                    // The first click only grabs the pointer and cannot become
                    // an unmatched guest button press.
                    if(ui_state.screen_hovered) {
                        set_mouse_capture(true);
                    }
                } else if(event.button.button == SDL_BUTTON_LEFT) {
                    machine.set_mouse_button(fk1::MouseButton::button_1, true);
                } else if(event.button.button == SDL_BUTTON_RIGHT) {
                    machine.set_mouse_button(fk1::MouseButton::button_2, true);
                }
            } else if(event.type == SDL_EVENT_MOUSE_BUTTON_UP && mouse_captured) {
                if(event.button.button == SDL_BUTTON_LEFT) {
                    machine.set_mouse_button(fk1::MouseButton::button_1, false);
                } else if(event.button.button == SDL_BUTTON_RIGHT) {
                    machine.set_mouse_button(fk1::MouseButton::button_2, false);
                }
            } else if(event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
                set_mouse_capture(false);
            }
        }

        running = running && !ui_state.quit_requested;

        synchronize_serial();

        // Host time only paces the frontend. Emulated devices advance on CPU cycles.
        if(!paused) {
            machine.run_cycles(fk1::Machine::cycles_per_frame);
        }

        synchronize_serial();

        auto printer_output_written = false;
        while(const auto byte = machine.take_printer_byte()) {
            printer_stream.put(static_cast<char>(*byte));
            printer_output_written = true;
        }
        if(printer_output_written) {
            printer_stream.flush();
            if(!printer_stream) {
                std::cerr << "Error: cannot write printer output file: "
                          << options.printer_path->string() << '\n';
                machine.set_printer_online(false);
                printer_write_failed = true;
                ui_state.status = "Printer output failed";
                running = false;
            }
        }

        const auto pixels = machine.frame_buffer();
        if(!SDL_UpdateTexture(
               texture,
               nullptr,
               pixels.data(),
               static_cast<int>(fk1::Machine::video_width * sizeof(fk1::Machine::Pixel)))) {
            print_sdl_error("SDL rendering failed");
            running = false;
        }

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        fk1::frontend::draw_ui(
            window,
            texture,
            machine,
            paused,
            mouse_captured,
            mounted_disks,
            ui_state);
        ImGui::Render();

        if(ui_state.mouse_capture_request) {
            set_mouse_capture(*ui_state.mouse_capture_request);
            ui_state.mouse_capture_request.reset();
        }
        running = running && !ui_state.quit_requested;

        SDL_SetRenderScale(
            renderer,
            imgui_io.DisplayFramebufferScale.x,
            imgui_io.DisplayFramebufferScale.y);
        SDL_SetRenderDrawColor(renderer, 18, 20, 22, 255);
        if(!SDL_RenderClear(renderer)) {
            print_sdl_error("SDL rendering failed");
            running = false;
        } else {
            ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
            SDL_RenderPresent(renderer);
        }

        next_frame += host_frame_period;
        const auto now = std::chrono::steady_clock::now();
        if(next_frame > now) {
            std::this_thread::sleep_until(next_frame);
        } else if(now - next_frame > host_frame_period * 5) {
            next_frame = now;
        }
    }

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    auto exit_status = printer_write_failed ? EXIT_FAILURE : EXIT_SUCCESS;
    for(const auto& [drive, name] : std::array{
            std::pair{fk1::FloppySubsystem::drive_a, 'A'},
            std::pair{fk1::FloppySubsystem::drive_b, 'B'},
        }) {
        if(mounted_disks[drive].path) {
            std::string disk_error;
            if(!fk1::frontend::eject_8sd(
                   machine,
                   drive,
                   mounted_disks[drive],
                   disk_error)) {
                std::cerr << "Error saving drive " << name << ": " << disk_error << '\n';
                exit_status = EXIT_FAILURE;
            }
        }
    }
    return exit_status;
}
