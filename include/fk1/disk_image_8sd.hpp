#pragma once

#include "fk1/floppy.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace fk1 {

class Ibm3740SectorImage final {
public:
    static constexpr std::size_t track_count = 77;
    static constexpr std::size_t sectors_per_track = 26;
    static constexpr std::size_t bytes_per_sector = 128;
    static constexpr std::size_t image_size =
        track_count * sectors_per_track * bytes_per_sector;
    static constexpr std::size_t fm_bytes_per_track = 5'208;
    static constexpr std::uint8_t default_sector_skew = 6;

    [[nodiscard]] static std::optional<FloppyDiskImage> import(
        std::span<const std::uint8_t> sector_image,
        std::uint8_t sector_skew = default_sector_skew);
    [[nodiscard]] static std::optional<std::vector<std::uint8_t>> export_image(
        const FloppyDiskImage& disk);
    [[nodiscard]] static std::uint16_t crc16(
        std::span<const std::uint8_t> bytes) noexcept;
};

static_assert(Ibm3740SectorImage::track_count == FloppyDiskImage::track_count);
static_assert(
    Ibm3740SectorImage::fm_bytes_per_track * FloppyTrack::byte_ticks
    < FloppyTrack::revolution_ticks);

} // namespace fk1
