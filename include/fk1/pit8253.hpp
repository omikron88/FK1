#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace fk1 {

class Pit8253 final {
public:
    struct ClockResult {
        std::uint64_t rising_edges{0};
        std::uint64_t falling_edges{0};
    };

    Pit8253() noexcept;

    void reset() noexcept;
    [[nodiscard]] std::uint8_t read(std::uint8_t register_index) noexcept;
    void write(std::uint8_t register_index, std::uint8_t value) noexcept;

    void set_gate(std::size_t channel, bool high) noexcept;
    [[nodiscard]] bool gate(std::size_t channel) const noexcept;
    [[nodiscard]] bool output(std::size_t channel) const noexcept;
    [[nodiscard]] std::uint16_t count(std::size_t channel) const noexcept;
    [[nodiscard]] ClockResult clock(std::size_t channel, std::uint64_t pulses = 1) noexcept;

private:
    enum class Access : std::uint8_t {
        latch,
        lsb,
        msb,
        lsb_msb,
    };

    struct Channel {
        Access access{Access::lsb_msb};
        std::uint8_t mode{0};
        bool bcd{false};
        bool gate{false};
        bool output{false};
        bool count_loaded{false};
        bool running{false};
        bool waiting_for_trigger{false};
        bool write_low_next{true};
        bool read_low_next{true};
        bool latched{false};
        bool latched_low_next{true};
        bool strobe_low{false};
        bool phase_high{true};
        std::uint8_t pending_low{0};
        std::uint16_t reload_raw{0};
        std::uint16_t latched_value{0};
        std::uint32_t reload{0};
        std::uint32_t current{0};
        std::uint32_t phase_remaining{0};
    };

    static void configure(Channel& channel, std::uint8_t control) noexcept;
    static void load(Channel& channel, std::uint16_t raw) noexcept;
    static void trigger(Channel& channel) noexcept;
    static void clock_once(Channel& channel) noexcept;
    [[nodiscard]] static std::uint16_t visible_count(const Channel& channel) noexcept;
    [[nodiscard]] static std::uint32_t decode_count(std::uint16_t raw, bool bcd) noexcept;
    [[nodiscard]] static std::uint16_t encode_count(std::uint32_t value, bool bcd) noexcept;
    [[nodiscard]] static std::uint8_t read_channel(Channel& channel) noexcept;
    static void write_channel(Channel& channel, std::uint8_t value) noexcept;

    std::array<Channel, 3> channels_{};
};

} // namespace fk1
