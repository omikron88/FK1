#pragma once

#include "fk1/floppy.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>

namespace fk1 {

class Machine;

namespace frontend {

struct MountedDisk {
    std::optional<std::filesystem::path> path;
    FloppyAccess access{FloppyAccess::read_only};
};

[[nodiscard]] bool create_blank_8sd(
    const std::filesystem::path& path,
    std::string& error);

[[nodiscard]] bool replace_8sd(
    Machine& machine,
    std::size_t drive,
    const std::filesystem::path& path,
    FloppyAccess access,
    MountedDisk& mounted,
    std::string& error);

[[nodiscard]] bool eject_8sd(
    Machine& machine,
    std::size_t drive,
    MountedDisk& mounted,
    std::string& error);

} // namespace frontend
} // namespace fk1
