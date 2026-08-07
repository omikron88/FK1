#include "disk_files.hpp"

#include "fk1/disk_image_8sd.hpp"
#include "fk1/machine.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expect(const bool condition, const char* message)
{
    if(!condition) {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

bool write_bytes(
    const std::filesystem::path& path,
    const std::vector<std::uint8_t>& bytes)
{
    std::ofstream stream{path, std::ios::binary | std::ios::trunc};
    stream.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(stream);
}

void test_disk_file_lifecycle()
{
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path();
    const auto valid_path = directory / ("fk1-disk-file-" + std::to_string(unique) + ".8sd");
    const auto invalid_path = directory / ("fk1-disk-file-" + std::to_string(unique) + ".bad");

    std::string error;
    expect(fk1::frontend::create_blank_8sd(valid_path, error),
           "the UI disk-file layer must create a blank .8sd image");
    std::ifstream blank{valid_path, std::ios::binary | std::ios::ate};
    const auto blank_size = blank.tellg();
    std::vector<std::uint8_t> blank_bytes(fk1::Ibm3740SectorImage::image_size);
    blank.seekg(0, std::ios::beg);
    const auto blank_read = static_cast<bool>(blank.read(
        reinterpret_cast<char*>(blank_bytes.data()),
        static_cast<std::streamsize>(blank_bytes.size())));
    expect(blank_size == static_cast<std::streamoff>(blank_bytes.size())
               && blank_read
               && std::ranges::all_of(blank_bytes, [](const auto byte) {
                      return byte == 0xE5;
                  }),
           "a blank .8sd image must contain exactly 256256 E5h bytes");
    blank.close();

    error.clear();
    expect(!fk1::frontend::create_blank_8sd(valid_path, error),
           "creating a blank image must not overwrite an existing file");
    expect(write_bytes(invalid_path, {0x00}), "the invalid disk-file fixture must be created");

    fk1::Machine machine;
    fk1::frontend::MountedDisk mounted;
    error.clear();
    expect(fk1::frontend::replace_8sd(
               machine,
               fk1::FloppySubsystem::drive_a,
               valid_path,
               fk1::FloppyAccess::read_only,
               mounted,
               error),
           "a valid .8sd image must open from the UI disk-file layer");
    expect(mounted.path == valid_path
               && machine.floppy().drive(fk1::FloppySubsystem::drive_a).media_present()
               && machine.floppy().drive(fk1::FloppySubsystem::drive_a).write_protected(),
           "a UI read-only mount must update both its slot and hardware write protect");

    error.clear();
    expect(!fk1::frontend::replace_8sd(
               machine,
               fk1::FloppySubsystem::drive_a,
               invalid_path,
               fk1::FloppyAccess::read_write,
               mounted,
               error),
           "an invalid replacement image must be rejected");
    expect(mounted.path == valid_path
               && machine.floppy().drive(fk1::FloppySubsystem::drive_a).media_present(),
           "rejecting a replacement must leave the current UI disk mounted");

    error.clear();
    expect(fk1::frontend::replace_8sd(
               machine,
               fk1::FloppySubsystem::drive_a,
               valid_path,
               fk1::FloppyAccess::read_write,
               mounted,
               error),
           "reopening the current image read-write must save and reload it safely");
    expect(!machine.floppy().drive(fk1::FloppySubsystem::drive_a).write_protected(),
           "a UI read-write mount must release hardware write protect");

    error.clear();
    expect(fk1::frontend::eject_8sd(
               machine,
               fk1::FloppySubsystem::drive_a,
               mounted,
               error),
           "ejecting a writable UI disk must export and save it");
    expect(!mounted.path
               && !machine.floppy().drive(fk1::FloppySubsystem::drive_a).media_present(),
           "a successful UI eject must clear both slot and hardware media state");

    std::ifstream saved{valid_path, std::ios::binary | std::ios::ate};
    expect(saved && saved.tellg()
               == static_cast<std::streamoff>(fk1::Ibm3740SectorImage::image_size),
           "a writable UI eject must preserve the complete .8sd image size");

    std::error_code cleanup_error;
    std::filesystem::remove(valid_path, cleanup_error);
    cleanup_error.clear();
    std::filesystem::remove(invalid_path, cleanup_error);
}

} // namespace

int main()
{
    test_disk_file_lifecycle();
    if(failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All FK-1 disk file tests passed\n";
    return EXIT_SUCCESS;
}
