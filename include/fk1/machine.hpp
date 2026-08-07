#pragma once

#include "fk1/floppy.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace fk1 {

enum class MouseButton : std::uint8_t {
    button_1,
    button_2,
};

class Machine final {
public:
    using Pixel = std::uint32_t;

    enum class MemoryMap {
        rom_video,
        ram,
    };

    static constexpr std::uint64_t cpu_clock_hz = 4'000'000;
    static constexpr std::uint64_t master_clock_hz = 12'000'000;
    static constexpr std::uint64_t master_ticks_per_cpu_cycle = master_clock_hz / cpu_clock_hz;
    static constexpr std::uint64_t video_refresh_hz = 50;
    static constexpr std::uint64_t cycles_per_frame = cpu_clock_hz / video_refresh_hz;
    static constexpr std::uint64_t master_ticks_per_frame = master_clock_hz / video_refresh_hz;
    static constexpr std::uint64_t mouse_edge_interval_ticks = master_clock_hz / 1'000U;

    static constexpr std::size_t video_width = 512;
    static constexpr std::size_t video_height = 256;
    static constexpr std::size_t video_ram_size = 16 * 1024;
    static constexpr std::size_t main_ram_size = 64 * 1024;
    static constexpr std::size_t maximum_rom_size = 16 * 1024;

    [[nodiscard]] static constexpr bool is_supported_rom_size(const std::size_t size) noexcept
    {
        return size == 2 * 1024 || size == 4 * 1024 || size == 8 * 1024 || size == 16 * 1024;
    }

    static constexpr Pixel black = 0xFF000000U;
    static constexpr Pixel white = 0xFFFFFFFFU;

    Machine();
    ~Machine();

    Machine(const Machine&) = delete;
    Machine& operator=(const Machine&) = delete;
    Machine(Machine&&) = delete;
    Machine& operator=(Machine&&) = delete;

    void reset() noexcept;
    void run_cycles(std::uint64_t cycles) noexcept;

    [[nodiscard]] std::uint64_t cycle_count() const noexcept;
    [[nodiscard]] std::uint64_t master_tick_count() const noexcept;
    [[nodiscard]] std::uint64_t frame_count() const noexcept;
    [[nodiscard]] std::uint16_t program_counter() const noexcept;
    [[nodiscard]] bool cpu_halted() const noexcept;

    [[nodiscard]] bool interrupt_pending() const noexcept;
    [[nodiscard]] std::uint8_t interrupt_vector() const noexcept;
    [[nodiscard]] std::uint8_t interrupt_mask() const noexcept;

    void keyboard_key_down(std::uint32_t key_id, std::uint8_t code) noexcept;
    void keyboard_key_up(std::uint32_t key_id) noexcept;
    [[nodiscard]] bool keyboard_key_held() const noexcept;

    void set_printer_online(bool online) noexcept;
    [[nodiscard]] bool printer_online() const noexcept;
    [[nodiscard]] std::optional<std::uint8_t> take_printer_byte() noexcept;

    void set_serial_connected(bool connected) noexcept;
    void set_serial_clear_to_send(bool clear) noexcept;
    [[nodiscard]] bool serial_connected() const noexcept;
    [[nodiscard]] bool queue_serial_received_byte(std::uint8_t value) noexcept;
    [[nodiscard]] std::optional<std::uint8_t> take_serial_transmitted_byte() noexcept;

    [[nodiscard]] bool insert_floppy(
        std::size_t drive,
        FloppyDiskImage image,
        FloppyAccess access);
    [[nodiscard]] std::optional<FloppyDiskImage> eject_floppy(std::size_t drive) noexcept;
    [[nodiscard]] bool set_floppy_index_pulse_width(std::uint32_t master_ticks) noexcept;
    [[nodiscard]] const FloppySubsystem& floppy() const noexcept;

    void set_mouse_signals(std::uint8_t signals) noexcept;
    void mouse_motion(std::int32_t delta_x, std::int32_t delta_y) noexcept;
    void set_mouse_button(MouseButton button, bool pressed) noexcept;
    [[nodiscard]] std::uint8_t mouse_signals() const noexcept;
    [[nodiscard]] bool mouse_interrupt_latched() const noexcept;
    [[nodiscard]] bool video_interrupt_latched() const noexcept;

    [[nodiscard]] bool set_rom_image(std::span<const std::uint8_t> image) noexcept;
    [[nodiscard]] std::span<const std::uint8_t> rom_image() const noexcept;
    [[nodiscard]] std::uint8_t read_rom(std::uint16_t address) const noexcept;

    [[nodiscard]] std::uint8_t read_memory(std::uint16_t address) const noexcept;
    void write_memory(std::uint16_t address, std::uint8_t value) noexcept;
    [[nodiscard]] MemoryMap memory_map() const noexcept;

    [[nodiscard]] std::uint8_t read_io(std::uint16_t port) noexcept;
    void write_io(std::uint16_t port, std::uint8_t value) noexcept;

    void write_video_ram(std::uint16_t address, std::uint8_t value) noexcept;
    [[nodiscard]] std::uint8_t read_video_ram(std::uint16_t address) const noexcept;

    void set_vertical_scroll(std::uint8_t value) noexcept;
    [[nodiscard]] std::uint8_t vertical_scroll() const noexcept;

    [[nodiscard]] std::span<const Pixel> frame_buffer() const noexcept;

private:
    struct CpuState;
    struct PeripheralState;

    void advance_time(std::uint64_t cpu_cycles) noexcept;
    void synchronize_cpu_io(std::uint8_t relative_cycle) noexcept;
    void synchronize_control_outputs() noexcept;
    void synchronize_disk_selection() noexcept;
    void process_floppy_byte(const TimedFmByte& byte) noexcept;
    [[nodiscard]] TimedFmByte transfer_floppy_write_byte(std::uint32_t tick) noexcept;
    void flush_pending_disk_writes() noexcept;
    void refresh_floppy_inputs() noexcept;
    void refresh_printer_inputs() noexcept;
    void advance_mouse(std::uint64_t master_ticks) noexcept;
    void update_mouse_signals(std::uint8_t signals) noexcept;
    void refresh_interrupt_sources() noexcept;
    void deliver_keyboard_code(std::uint8_t code) noexcept;
    void render_video() noexcept;

    std::array<std::uint8_t, video_ram_size> video_ram_{};
    std::array<std::uint8_t, main_ram_size> main_ram_{};
    std::array<std::uint8_t, maximum_rom_size> rom_{};
    std::array<Pixel, video_width * video_height> frame_buffer_{};
    std::unique_ptr<CpuState> cpu_;
    std::unique_ptr<PeripheralState> peripherals_;
    std::size_t rom_size_{0};
    MemoryMap memory_map_{MemoryMap::rom_video};
    std::uint64_t cycle_count_{0};
    std::uint64_t master_tick_count_{0};
    std::uint64_t frame_tick_accumulator_{0};
    std::uint64_t frame_count_{0};
    std::uint64_t cpu_cycle_overrun_{0};
    std::uint8_t vertical_scroll_{0};
};

static_assert(Machine::cpu_clock_hz % Machine::video_refresh_hz == 0);
static_assert(Machine::master_clock_hz % Machine::cpu_clock_hz == 0);
static_assert(Machine::master_clock_hz % Machine::video_refresh_hz == 0);

} // namespace fk1
