#pragma once

#include "disk_files.hpp"

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

struct SDL_Texture;
struct SDL_Window;

namespace fk1 {

class Machine;

namespace frontend {

struct UiState {
    bool fullscreen{false};
    bool scanlines{false};
    bool show_about{false};
    bool quit_requested{false};
    bool native_file_dialog_open{false};
    bool disk_dialog_was_running{false};
    bool screen_hovered{false};
    bool serial_enabled{false};
    std::array<bool, 2> open_writable{};
    std::array<std::string, 2> disk_dialog_paths{};
    std::vector<std::filesystem::path> protected_disk_paths;
    std::optional<bool> mouse_capture_request;
    std::string status{"Ready"};
};

void draw_ui(
    SDL_Window* window,
    SDL_Texture* screen_texture,
    Machine& machine,
    bool& paused,
    bool mouse_captured,
    std::array<MountedDisk, 2>& disks,
    UiState& state);

} // namespace frontend
} // namespace fk1
