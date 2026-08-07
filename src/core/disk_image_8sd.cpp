#include "fk1/disk_image_8sd.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <vector>

namespace fk1 {
namespace {

constexpr std::uint8_t ordinary_clocks = 0xFF;
constexpr std::uint8_t index_mark_clocks = 0xD7;
constexpr std::uint8_t sector_mark_clocks = 0xC7;

void update_crc(std::uint16_t& crc, const std::uint8_t value) noexcept
{
    crc = static_cast<std::uint16_t>(crc ^ (static_cast<std::uint16_t>(value) << 8U));
    for(unsigned bit = 0; bit < 8; ++bit) {
        crc = (crc & 0x8000U) != 0
            ? static_cast<std::uint16_t>((crc << 1U) ^ 0x1021U)
            : static_cast<std::uint16_t>(crc << 1U);
    }
}

} // namespace

std::uint16_t Ibm3740SectorImage::crc16(
    const std::span<const std::uint8_t> bytes) noexcept
{
    auto crc = std::uint16_t{0xFFFFU};
    for(const auto byte : bytes) {
        update_crc(crc, byte);
    }
    return crc;
}

std::optional<FloppyDiskImage> Ibm3740SectorImage::import(
    const std::span<const std::uint8_t> sector_image,
    const std::uint8_t sector_skew)
{
    if(sector_image.size() != image_size) {
        return std::nullopt;
    }

    std::array<std::uint8_t, sectors_per_track> sector_map{};
    std::array<bool, sectors_per_track> occupied{};
    auto map_index = std::size_t{0};
    for(std::uint8_t sector = 1; sector <= sectors_per_track; ++sector) {
        if(map_index >= sector_map.size() || occupied[map_index]) {
            return std::nullopt;
        }
        sector_map[map_index] = sector;
        occupied[map_index] = true;
        map_index += sector_skew;
        if(map_index >= sectors_per_track) {
            map_index -= sectors_per_track - 1U;
        }
    }

    FloppyDiskImage disk;
    for(std::size_t track_number = 0; track_number < track_count; ++track_number) {
        std::vector<TimedFmByte> track;
        track.reserve(fm_bytes_per_track);

        const auto append = [&track](
                                const std::uint8_t value,
                                const std::uint8_t clocks = ordinary_clocks) {
            track.push_back({
                static_cast<std::uint32_t>(track.size() * FloppyTrack::byte_ticks),
                value,
                clocks,
            });
        };
        const auto append_repeat = [&append](
                                       const std::size_t count,
                                       const std::uint8_t value) {
            for(std::size_t index = 0; index < count; ++index) {
                append(value);
            }
        };
        const auto append_crc = [&append](const std::span<const std::uint8_t> bytes) {
            const auto crc = crc16(bytes);
            append(static_cast<std::uint8_t>(crc >> 8U));
            append(static_cast<std::uint8_t>(crc));
        };

        // IBM 3740 single-density FM: index gap and Index Address Mark.
        append_repeat(40, 0xFF);
        append_repeat(6, 0x00);
        append(0xFC, index_mark_clocks);
        append_repeat(26, 0xFF);

        for(std::size_t sector_index = 0; sector_index < sectors_per_track; ++sector_index) {
            const auto sector_number = sector_map[sector_index];

            append_repeat(6, 0x00);
            append(0xFE, sector_mark_clocks);
            const std::array<std::uint8_t, 5> id_field{{
                0xFE,
                static_cast<std::uint8_t>(track_number),
                0x00,
                sector_number,
                0x00,
            }};
            for(std::size_t index = 1; index < id_field.size(); ++index) {
                append(id_field[index]);
            }
            append_crc(id_field);

            append_repeat(11, 0xFF);
            append_repeat(6, 0x00);
            append(0xFB, sector_mark_clocks);

            const auto raw_offset =
                (track_number * sectors_per_track + (sector_number - 1U)) * bytes_per_sector;
            std::array<std::uint8_t, bytes_per_sector + 1U> data_field{};
            data_field[0] = 0xFB;
            for(std::size_t index = 0; index < bytes_per_sector; ++index) {
                const auto value = sector_image[raw_offset + index];
                data_field[index + 1U] = value;
                append(value);
            }
            append_crc(data_field);
            append_repeat(27, 0xFF);
        }

        append_repeat(247, 0xFF);
        assert(track.size() == fm_bytes_per_track);
        if(!disk.set_track(track_number, track)) {
            return std::nullopt;
        }
    }
    return disk;
}

std::optional<std::vector<std::uint8_t>> Ibm3740SectorImage::export_image(
    const FloppyDiskImage& disk)
{
    std::vector<std::uint8_t> sector_image(image_size);
    std::array<bool, track_count * sectors_per_track> found{};

    for(std::size_t physical_track = 0; physical_track < track_count; ++physical_track) {
        const auto* track = disk.track(physical_track);
        if(track == nullptr) {
            return std::nullopt;
        }

        auto pending_sector = std::optional<std::uint8_t>{};
        const auto bytes = track->bytes();
        for(std::size_t index = 0; index < bytes.size(); ++index) {
            const auto& byte = bytes[index];
            if(byte.data == 0xFEU && byte.clocks == sector_mark_clocks) {
                pending_sector.reset();
                if(index + 6U >= bytes.size()) {
                    continue;
                }
                const auto cylinder = bytes[index + 1U].data;
                const auto head = bytes[index + 2U].data;
                const auto sector = bytes[index + 3U].data;
                const auto length = bytes[index + 4U].data;
                if(cylinder == physical_track && head == 0 && sector >= 1U
                   && sector <= sectors_per_track && length == 0) {
                    pending_sector = sector;
                }
                index += 6U;
                continue;
            }

            if((byte.data == 0xFBU || byte.data == 0xF8U)
               && byte.clocks == sector_mark_clocks && pending_sector) {
                if(index + bytes_per_sector + 2U >= bytes.size()) {
                    pending_sector.reset();
                    continue;
                }
                const auto sector_index = static_cast<std::size_t>(*pending_sector - 1U);
                const auto raw_offset =
                    (physical_track * sectors_per_track + sector_index) * bytes_per_sector;
                for(std::size_t data_index = 0; data_index < bytes_per_sector; ++data_index) {
                    sector_image[raw_offset + data_index] = bytes[index + 1U + data_index].data;
                }
                found[physical_track * sectors_per_track + sector_index] = true;
                pending_sector.reset();
                index += bytes_per_sector + 2U;
            }
        }
    }

    if(!std::all_of(found.begin(), found.end(), [](const bool sector_found) {
           return sector_found;
       })) {
        return std::nullopt;
    }
    return sector_image;
}

} // namespace fk1
