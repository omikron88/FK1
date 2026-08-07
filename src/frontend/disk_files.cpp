#include "disk_files.hpp"

#include "fk1/disk_image_8sd.hpp"
#include "fk1/machine.hpp"

#include <cstdint>
#include <fstream>
#include <span>
#include <utility>
#include <vector>

namespace fk1::frontend {
namespace {

std::optional<FloppyDiskImage> read_8sd(
    const std::filesystem::path& path,
    std::string& error)
{
    std::ifstream stream{path, std::ios::binary | std::ios::ate};
    if(!stream) {
        error = "cannot open disk image: " + path.string();
        return std::nullopt;
    }

    const auto end = stream.tellg();
    if(end != static_cast<std::streamoff>(Ibm3740SectorImage::image_size)) {
        error = "disk image must contain exactly 77 x 26 x 128 bytes: "
            + path.string();
        return std::nullopt;
    }

    std::vector<std::uint8_t> image(Ibm3740SectorImage::image_size);
    stream.seekg(0, std::ios::beg);
    if(!stream.read(
           reinterpret_cast<char*>(image.data()),
           static_cast<std::streamsize>(image.size()))) {
        error = "cannot read disk image: " + path.string();
        return std::nullopt;
    }

    auto disk = Ibm3740SectorImage::import(std::span<const std::uint8_t>{image});
    if(!disk) {
        error = "cannot construct IBM 3740 FM tracks from: " + path.string();
    }
    return disk;
}

void restore_after_failed_eject(
    Machine& machine,
    const std::size_t drive,
    std::optional<FloppyDiskImage>& disk,
    const FloppyAccess access,
    std::string& error)
{
    if(disk && !machine.insert_floppy(drive, std::move(*disk), access)) {
        error += "; the disk could not be restored in the drive";
    }
}

bool same_existing_file(
    const std::filesystem::path& left,
    const std::filesystem::path& right) noexcept
{
    std::error_code error;
    const auto equivalent = std::filesystem::equivalent(left, right, error);
    return !error && equivalent;
}

} // namespace

bool create_blank_8sd(
    const std::filesystem::path& path,
    std::string& error)
{
    std::error_code filesystem_error;
    if(std::filesystem::exists(path, filesystem_error)) {
        error = "disk image already exists: " + path.string();
        return false;
    }
    if(filesystem_error) {
        error = "cannot inspect disk image path: " + path.string();
        return false;
    }

    const std::vector<std::uint8_t> image(
        Ibm3740SectorImage::image_size,
        0xE5);
    std::ofstream stream{path, std::ios::binary | std::ios::trunc};
    if(stream) {
        stream.write(
            reinterpret_cast<const char*>(image.data()),
            static_cast<std::streamsize>(image.size()));
        stream.flush();
    }
    if(!stream) {
        stream.close();
        std::filesystem::remove(path, filesystem_error);
        error = "cannot create blank disk image: " + path.string();
        return false;
    }
    return true;
}

bool eject_8sd(
    Machine& machine,
    const std::size_t drive,
    MountedDisk& mounted,
    std::string& error)
{
    auto disk = machine.eject_floppy(drive);
    if(!disk) {
        mounted.path.reset();
        return true;
    }

    if(mounted.path && mounted.access == FloppyAccess::read_write) {
        const auto raw = Ibm3740SectorImage::export_image(*disk);
        if(!raw) {
            error = "cannot convert the low-level disk back to complete .8sd sectors: "
                + mounted.path->string();
            restore_after_failed_eject(machine, drive, disk, mounted.access, error);
            return false;
        }

        std::fstream stream{
            *mounted.path,
            std::ios::binary | std::ios::in | std::ios::out,
        };
        if(stream) {
            stream.seekp(0, std::ios::beg);
            stream.write(
                reinterpret_cast<const char*>(raw->data()),
                static_cast<std::streamsize>(raw->size()));
            stream.flush();
        }
        if(!stream) {
            error = "cannot save writable disk image: " + mounted.path->string();
            restore_after_failed_eject(machine, drive, disk, mounted.access, error);
            return false;
        }
    }

    mounted.path.reset();
    return true;
}

bool replace_8sd(
    Machine& machine,
    const std::size_t drive,
    const std::filesystem::path& path,
    const FloppyAccess access,
    MountedDisk& mounted,
    std::string& error)
{
    // Validate and expand the new image before touching the disk which is
    // already mounted. A bad selection must not eject a working disk. When
    // reopening that same file, save it first and then read the saved state.
    const auto reopening_same_file = mounted.path
        && same_existing_file(*mounted.path, path);
    std::optional<FloppyDiskImage> disk;
    if(!reopening_same_file) {
        disk = read_8sd(path, error);
        if(!disk) {
            return false;
        }
    }
    if(mounted.path && !eject_8sd(machine, drive, mounted, error)) {
        return false;
    }
    if(reopening_same_file) {
        disk = read_8sd(path, error);
        if(!disk) {
            return false;
        }
    }
    if(!machine.insert_floppy(drive, std::move(*disk), access)) {
        error = "cannot insert disk image into drive " + std::to_string(drive);
        return false;
    }

    mounted.path = path;
    mounted.access = access;
    return true;
}

} // namespace fk1::frontend
