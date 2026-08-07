#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace fk1 {

struct TimedFmByte {
    std::uint32_t tick{0};
    std::uint8_t data{0};
    std::uint8_t clocks{0xFF};

    [[nodiscard]] friend constexpr bool operator==(
        const TimedFmByte&,
        const TimedFmByte&) noexcept = default;
};

using FloppyByteCallback = void (*)(void* context, const TimedFmByte& byte) noexcept;

class FloppyTrack final {
public:
    static constexpr std::uint32_t byte_ticks = 384;
    static constexpr std::uint32_t revolution_ticks = 2'000'000;

    [[nodiscard]] bool set_bytes(std::span<const TimedFmByte> bytes);
    [[nodiscard]] bool write_byte(const TimedFmByte& byte);
    void clear() noexcept;

    [[nodiscard]] std::span<const TimedFmByte> bytes() const noexcept;

private:
    std::vector<TimedFmByte> bytes_;
};

class FloppyDiskImage final {
public:
    static constexpr std::size_t track_count = 77;

    [[nodiscard]] bool set_track(
        std::size_t track,
        std::span<const TimedFmByte> bytes);
    [[nodiscard]] const FloppyTrack* track(std::size_t track) const noexcept;
    [[nodiscard]] FloppyTrack* track(std::size_t track) noexcept;

private:
    std::array<FloppyTrack, track_count> tracks_{};
};

enum class FloppyAccess : std::uint8_t {
    read_only,
    read_write,
};

class FloppyDrive final {
public:
    static constexpr std::uint32_t revolution_ticks = FloppyTrack::revolution_ticks;
    static constexpr std::uint32_t revolutions_per_minute = 360;
    static constexpr std::uint8_t last_track = 76;

    void insert(FloppyDiskImage image, FloppyAccess access);
    [[nodiscard]] std::optional<FloppyDiskImage> eject() noexcept;

    void visit_bytes(
        std::uint64_t master_ticks,
        void* context,
        FloppyByteCallback callback) const noexcept;
    void advance(std::uint64_t master_ticks) noexcept;
    [[nodiscard]] bool write_byte(const TimedFmByte& byte);
    void step_toward_track_zero() noexcept;
    void step_toward_higher_track() noexcept;

    [[nodiscard]] bool media_present() const noexcept;
    [[nodiscard]] bool write_protected() const noexcept;
    [[nodiscard]] bool track_zero() const noexcept;
    [[nodiscard]] bool index_active(std::uint32_t pulse_width_ticks) const noexcept;
    [[nodiscard]] std::uint8_t head_track() const noexcept;
    [[nodiscard]] std::uint32_t rotation_phase() const noexcept;
    [[nodiscard]] std::uint64_t completed_revolutions() const noexcept;
    [[nodiscard]] const FloppyDiskImage* image() const noexcept;
    [[nodiscard]] FloppyDiskImage* image() noexcept;

private:
    std::optional<FloppyDiskImage> image_;
    FloppyAccess access_{FloppyAccess::read_only};
    std::uint64_t completed_revolutions_{0};
    std::uint32_t rotation_phase_{0};
    std::uint8_t head_track_{0};
};

class FloppySubsystem final {
public:
    static constexpr std::size_t drive_count = 2;
    static constexpr std::size_t drive_a = 0;
    static constexpr std::size_t drive_b = 1;

    void reset_controller() noexcept;
    void advance(
        std::uint64_t master_ticks,
        void* context = nullptr,
        FloppyByteCallback callback = nullptr) noexcept;

    [[nodiscard]] bool insert(
        std::size_t drive,
        FloppyDiskImage image,
        FloppyAccess access);
    [[nodiscard]] std::optional<FloppyDiskImage> eject(std::size_t drive) noexcept;
    [[nodiscard]] bool write_selected_byte(const TimedFmByte& byte);

    [[nodiscard]] bool set_index_pulse_width(std::uint32_t master_ticks) noexcept;
    void select_from_disk_ppi(std::uint8_t effective_port_c) noexcept;
    void set_control_port_c(std::uint8_t output, bool lower_output_enabled) noexcept;

    [[nodiscard]] std::uint8_t control_port_c_inputs() const noexcept;
    [[nodiscard]] std::size_t selected_drive_index() const noexcept;
    [[nodiscard]] std::uint32_t index_pulse_width() const noexcept;
    [[nodiscard]] bool track_greater_than_43_active() const noexcept;
    [[nodiscard]] bool head_loaded() const noexcept;
    [[nodiscard]] const FloppyDrive& drive(std::size_t drive) const noexcept;

private:
    [[nodiscard]] FloppyDrive& selected_drive() noexcept;
    [[nodiscard]] const FloppyDrive& selected_drive() const noexcept;

    std::array<FloppyDrive, drive_count> drives_{};
    std::size_t selected_drive_{drive_b};
    std::uint32_t index_pulse_width_ticks_{0};
    bool step_asserted_{false};
    bool direction_to_higher_track_{false};
    bool track_greater_than_43_active_{false};
    bool head_loaded_{false};
};

static_assert(FloppyDrive::revolution_ticks == 2'000'000);
static_assert(FloppyDiskImage::track_count == 77);

} // namespace fk1
