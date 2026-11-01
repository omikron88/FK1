#pragma once

#include <cstdint>
#include <optional>

namespace fk1 {

// PC AT set-2 decoder matching the logical bytes of KEYB_FIN.ASM.
// Electrical inversion on P1 is handled by the real adapter, not by this decoder.
class AtKeyboard final {
public:
    [[nodiscard]] std::optional<std::uint8_t> translate(
        std::uint8_t scan, bool extended = false, bool shift = false,
        bool caps = false, bool control = false, bool num = false,
        bool scroll = false) noexcept;
    void reset() noexcept;
    void release_scroll() noexcept { hex_pending_ = false; }
    [[nodiscard]] bool czech() const noexcept { return czech_; }
private:
    bool czech_{false};
    bool acute_{false};
    bool caron_{false};
    bool hex_pending_{false};
    std::uint8_t hex_high_{0};
};

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
