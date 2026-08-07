#include "fk1/pit8253.hpp"

namespace fk1 {

Pit8253::Pit8253() noexcept
{
    reset();
}

void Pit8253::reset() noexcept
{
    channels_ = {};
}

std::uint8_t Pit8253::read(const std::uint8_t register_index) noexcept
{
    const auto index = static_cast<std::size_t>(register_index & 0x03U);
    return index < channels_.size() ? read_channel(channels_[index]) : 0xFF;
}

void Pit8253::write(const std::uint8_t register_index, const std::uint8_t value) noexcept
{
    const auto index = static_cast<std::size_t>(register_index & 0x03U);
    if(index < channels_.size()) {
        write_channel(channels_[index], value);
        return;
    }

    const auto selected = static_cast<std::size_t>((value >> 6U) & 0x03U);
    if(selected >= channels_.size()) {
        // The 8253 has no 8254-style read-back command.
        return;
    }

    const auto access = static_cast<Access>((value >> 4U) & 0x03U);
    if(access == Access::latch) {
        auto& channel = channels_[selected];
        if(!channel.latched) {
            channel.latched_value = visible_count(channel);
            channel.latched = true;
            channel.latched_low_next = true;
        }
        return;
    }

    configure(channels_[selected], value);
}

void Pit8253::set_gate(const std::size_t channel_index, const bool high) noexcept
{
    if(channel_index >= channels_.size()) {
        return;
    }

    auto& channel = channels_[channel_index];
    const auto rising = !channel.gate && high;
    const auto falling = channel.gate && !high;
    channel.gate = high;

    switch(channel.mode) {
    case 0:
    case 4:
        channel.running = channel.count_loaded && high && !channel.strobe_low;
        break;
    case 1:
    case 5:
        if(rising && channel.count_loaded) {
            trigger(channel);
        }
        break;
    case 2:
    case 3:
        if(falling) {
            channel.running = false;
            channel.output = true;
        } else if(rising && channel.count_loaded) {
            channel.current = channel.reload;
            channel.phase_high = true;
            channel.phase_remaining = channel.mode == 3 ? (channel.reload + 1U) / 2U : 0;
            channel.output = true;
            channel.running = true;
        }
        break;
    default:
        break;
    }
}

bool Pit8253::gate(const std::size_t channel) const noexcept
{
    return channel < channels_.size() && channels_[channel].gate;
}

bool Pit8253::output(const std::size_t channel) const noexcept
{
    return channel < channels_.size() && channels_[channel].output;
}

std::uint16_t Pit8253::count(const std::size_t channel) const noexcept
{
    return channel < channels_.size() ? visible_count(channels_[channel]) : 0;
}

Pit8253::ClockResult Pit8253::clock(
    const std::size_t channel_index,
    const std::uint64_t pulses) noexcept
{
    ClockResult result;
    if(channel_index >= channels_.size()) {
        return result;
    }

    auto& channel = channels_[channel_index];
    for(std::uint64_t pulse = 0; pulse < pulses; ++pulse) {
        const auto old_output = channel.output;
        clock_once(channel);
        if(!old_output && channel.output) {
            ++result.rising_edges;
        } else if(old_output && !channel.output) {
            ++result.falling_edges;
        }
    }
    return result;
}

void Pit8253::configure(Channel& channel, const std::uint8_t control) noexcept
{
    channel.access = static_cast<Access>((control >> 4U) & 0x03U);
    channel.mode = static_cast<std::uint8_t>((control >> 1U) & 0x07U);
    if(channel.mode == 6) {
        channel.mode = 2;
    } else if(channel.mode == 7) {
        channel.mode = 3;
    }
    channel.bcd = (control & 0x01U) != 0;
    channel.output = channel.mode != 0;
    channel.count_loaded = false;
    channel.running = false;
    channel.waiting_for_trigger = channel.mode == 1 || channel.mode == 5;
    channel.write_low_next = true;
    channel.read_low_next = true;
    channel.latched = false;
    channel.latched_low_next = true;
    channel.strobe_low = false;
    channel.phase_high = true;
    channel.pending_low = 0;
    channel.reload_raw = 0;
    channel.reload = 0;
    channel.current = 0;
    channel.phase_remaining = 0;
}

void Pit8253::load(Channel& channel, const std::uint16_t raw) noexcept
{
    channel.reload_raw = raw;
    channel.reload = decode_count(raw, channel.bcd);
    channel.current = channel.reload;
    channel.count_loaded = true;
    channel.strobe_low = false;
    channel.phase_high = true;

    switch(channel.mode) {
    case 0:
        channel.output = false;
        channel.running = channel.gate;
        break;
    case 1:
        channel.output = true;
        channel.running = false;
        channel.waiting_for_trigger = true;
        break;
    case 2:
        channel.output = true;
        channel.running = channel.gate;
        break;
    case 3:
        channel.output = true;
        channel.running = channel.gate;
        channel.phase_remaining = (channel.reload + 1U) / 2U;
        break;
    case 4:
        channel.output = true;
        channel.running = channel.gate;
        break;
    case 5:
        channel.output = true;
        channel.running = false;
        channel.waiting_for_trigger = true;
        break;
    default:
        break;
    }
}

void Pit8253::trigger(Channel& channel) noexcept
{
    channel.current = channel.reload;
    channel.output = channel.mode != 1;
    if(channel.mode == 1) {
        channel.output = false;
    }
    channel.running = true;
    channel.waiting_for_trigger = false;
    channel.strobe_low = false;
}

void Pit8253::clock_once(Channel& channel) noexcept
{
    if(!channel.running || !channel.count_loaded) {
        return;
    }

    switch(channel.mode) {
    case 0:
    case 1:
        if(channel.current > 1) {
            --channel.current;
        } else {
            channel.current = 0;
            channel.output = true;
            channel.running = false;
        }
        break;
    case 2:
        if(!channel.output) {
            channel.output = true;
            if(channel.current > 1) {
                --channel.current;
            }
        } else if(channel.current > 1) {
            --channel.current;
        } else {
            channel.current = channel.reload;
            channel.output = false;
        }
        break;
    case 3:
        if(channel.phase_remaining > 1) {
            --channel.phase_remaining;
        } else {
            channel.output = !channel.output;
            channel.phase_high = channel.output;
            channel.phase_remaining = channel.phase_high
                ? (channel.reload + 1U) / 2U
                : channel.reload / 2U;
            if(channel.phase_remaining == 0) {
                channel.phase_remaining = 1;
            }
        }
        break;
    case 4:
    case 5:
        if(channel.strobe_low) {
            channel.output = true;
            channel.strobe_low = false;
            channel.running = false;
        } else if(channel.current > 1) {
            --channel.current;
        } else {
            channel.current = 0;
            channel.output = false;
            channel.strobe_low = true;
        }
        break;
    default:
        break;
    }
}

std::uint16_t Pit8253::visible_count(const Channel& channel) noexcept
{
    if(channel.mode != 3) {
        return encode_count(channel.current, channel.bcd);
    }

    auto value = channel.phase_remaining * 2U;
    if(channel.phase_high && (channel.reload & 1U) != 0 && value != 0) {
        --value;
    }
    return encode_count(value, channel.bcd);
}

std::uint32_t Pit8253::decode_count(const std::uint16_t raw, const bool bcd) noexcept
{
    if(!bcd) {
        return raw == 0 ? 65'536U : raw;
    }

    const auto value = static_cast<std::uint32_t>(
        ((raw >> 12U) & 0x0FU) * 1000U
        + ((raw >> 8U) & 0x0FU) * 100U
        + ((raw >> 4U) & 0x0FU) * 10U
        + (raw & 0x0FU));
    return value == 0 ? 10'000U : value;
}

std::uint16_t Pit8253::encode_count(std::uint32_t value, const bool bcd) noexcept
{
    if(!bcd) {
        return static_cast<std::uint16_t>(value & 0xFFFFU);
    }

    if(value == 10'000U) {
        return 0;
    }
    value %= 10'000U;
    return static_cast<std::uint16_t>(
        ((value / 1000U) << 12U)
        | (((value / 100U) % 10U) << 8U)
        | (((value / 10U) % 10U) << 4U)
        | (value % 10U));
}

std::uint8_t Pit8253::read_channel(Channel& channel) noexcept
{
    const auto value = channel.latched ? channel.latched_value : visible_count(channel);
    switch(channel.access) {
    case Access::lsb:
        channel.latched = false;
        return static_cast<std::uint8_t>(value & 0x00FFU);
    case Access::msb:
        channel.latched = false;
        return static_cast<std::uint8_t>(value >> 8U);
    case Access::lsb_msb: {
        auto& low_next = channel.latched ? channel.latched_low_next : channel.read_low_next;
        if(low_next) {
            low_next = false;
            return static_cast<std::uint8_t>(value & 0x00FFU);
        }
        low_next = true;
        channel.latched = false;
        return static_cast<std::uint8_t>(value >> 8U);
    }
    case Access::latch:
        return 0xFF;
    }
    return 0xFF;
}

void Pit8253::write_channel(Channel& channel, const std::uint8_t value) noexcept
{
    switch(channel.access) {
    case Access::lsb:
        load(channel, value);
        break;
    case Access::msb:
        load(channel, static_cast<std::uint16_t>(value) << 8U);
        break;
    case Access::lsb_msb:
        if(channel.write_low_next) {
            channel.pending_low = value;
            channel.write_low_next = false;
        } else {
            const auto raw = static_cast<std::uint16_t>(
                static_cast<std::uint16_t>(value) << 8U | channel.pending_low);
            channel.write_low_next = true;
            load(channel, raw);
        }
        break;
    case Access::latch:
        break;
    }
}

} // namespace fk1
