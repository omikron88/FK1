#pragma once

#include <cstdint>
#include <optional>

namespace fk1 {

class Keyboard final {
public:
    enum class SpecialKey {
        roll,
        copy,
        break_key,
        up,
        down,
        right,
        left,
        home,
        user_1,
        user_2,
        user_3,
    };

    static constexpr std::uint64_t repeat_delay_ticks = 6'000'000;
    static constexpr std::uint64_t repeat_interval_ticks = 3'000'000;

    Keyboard() noexcept;

    void reset() noexcept;
    [[nodiscard]] std::optional<std::uint8_t> key_down(
        std::uint32_t key_id,
        std::uint8_t code) noexcept;
    void key_up(std::uint32_t key_id) noexcept;
    [[nodiscard]] std::uint64_t advance(std::uint64_t master_ticks) noexcept;

    [[nodiscard]] bool key_held() const noexcept;
    [[nodiscard]] std::uint8_t repeat_code() const noexcept;

    [[nodiscard]] static std::optional<std::uint8_t> ascii_code(
        std::uint32_t character,
        bool control = false) noexcept;
    [[nodiscard]] static std::uint8_t special_code(
        SpecialKey key,
        bool shift = false) noexcept;

private:
    std::uint64_t ticks_until_repeat_{repeat_delay_ticks};
    std::uint32_t held_key_id_{0};
    std::uint8_t held_code_{0};
    bool key_held_{false};
};

} // namespace fk1
