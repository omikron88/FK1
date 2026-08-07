#include "fk1/keyboard.hpp"

namespace fk1 {

Keyboard::Keyboard() noexcept
{
    reset();
}

void Keyboard::reset() noexcept
{
    ticks_until_repeat_ = repeat_delay_ticks;
    held_key_id_ = 0;
    held_code_ = 0;
    key_held_ = false;
}

std::optional<std::uint8_t> Keyboard::key_down(
    const std::uint32_t key_id,
    const std::uint8_t code) noexcept
{
    if(key_held_ && held_key_id_ == key_id) {
        return std::nullopt;
    }

    held_key_id_ = key_id;
    held_code_ = code;
    ticks_until_repeat_ = repeat_delay_ticks;
    key_held_ = true;
    return code;
}

void Keyboard::key_up(const std::uint32_t key_id) noexcept
{
    if(key_held_ && held_key_id_ == key_id) {
        key_held_ = false;
        ticks_until_repeat_ = repeat_delay_ticks;
    }
}

std::uint64_t Keyboard::advance(std::uint64_t master_ticks) noexcept
{
    if(!key_held_ || master_ticks < ticks_until_repeat_) {
        if(key_held_) {
            ticks_until_repeat_ -= master_ticks;
        }
        return 0;
    }

    master_ticks -= ticks_until_repeat_;
    const auto repeats = 1U + master_ticks / repeat_interval_ticks;
    const auto remainder = master_ticks % repeat_interval_ticks;
    ticks_until_repeat_ = repeat_interval_ticks - remainder;
    return repeats;
}

bool Keyboard::key_held() const noexcept
{
    return key_held_;
}

std::uint8_t Keyboard::repeat_code() const noexcept
{
    return held_code_;
}

std::optional<std::uint8_t> Keyboard::ascii_code(
    std::uint32_t character,
    const bool control) noexcept
{
    if(control) {
        if(character >= 'a' && character <= 'z') {
            character -= static_cast<std::uint32_t>('a' - 'A');
        }
        if(character >= '@' && character <= '_') {
            return static_cast<std::uint8_t>(character & 0x1FU);
        }
        return std::nullopt;
    }

    if(character == 0x08U) {
        return 0x7F;
    }
    if(character == 0x09U || character == 0x0DU || character == 0x1BU
       || (character >= 0x20U && character <= 0x7EU)) {
        return static_cast<std::uint8_t>(character);
    }
    return std::nullopt;
}

std::uint8_t Keyboard::special_code(const SpecialKey key, const bool shift) noexcept
{
    switch(key) {
    case SpecialKey::roll:
        return shift ? 0x81 : 0x80;
    case SpecialKey::copy:
        return shift ? 0x83 : 0x82;
    case SpecialKey::break_key:
        return shift ? 0x85 : 0x84;
    case SpecialKey::up:
        return 0xC1;
    case SpecialKey::down:
        return 0xC2;
    case SpecialKey::right:
        return 0xC3;
    case SpecialKey::left:
        return 0xC4;
    case SpecialKey::home:
        return 0x8D;
    case SpecialKey::user_1:
        return 0xD0;
    case SpecialKey::user_2:
        return 0xD1;
    case SpecialKey::user_3:
        return 0xD2;
    }
    return 0;
}

} // namespace fk1
