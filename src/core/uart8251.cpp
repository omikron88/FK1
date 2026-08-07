#include "fk1/uart8251.hpp"

namespace fk1 {

Uart8251::Uart8251() noexcept
{
    reset();
}

void Uart8251::reset() noexcept
{
    initialization_phase_ = InitializationPhase::mode;
    receive_phase_ = ReceivePhase::idle;
    mode_word_ = 0;
    command_word_ = 0;
    sync_characters_ = {};
    sync_character_count_ = 0;
    sync_transmit_index_ = 0;
    sync_hunt_index_ = 0;
    transmit_buffer_.reset();
    transmit_frame_ = {};
    transmit_frame_size_ = 0;
    transmit_frame_index_ = 0;
    transmit_character_ = 0;
    transmit_active_ = false;
    transmit_payload_ = false;
    txd_high_ = true;
    transmit_clock_count_ = 0;
    transmitted_bytes_.clear();
    receive_buffer_ = 0;
    receive_shift_ = 0;
    receive_bit_index_ = 0;
    receive_parity_ones_ = 0;
    receive_clock_count_ = 0;
    receive_ready_ = false;
    receive_parity_error_ = false;
    receive_overrun_error_ = false;
    receive_framing_error_ = false;
    pending_receive_parity_error_ = false;
    rxd_high_ = true;
    break_detected_ = false;
    sync_detected_ = false;
    sync_hunt_ = false;
    queued_receive_bytes_.clear();
    queued_receive_frame_ = {};
    queued_receive_frame_size_ = 0;
    queued_receive_frame_index_ = 0;
    queued_receive_bit_clocks_ = 0;
    queued_receive_active_ = false;
    cts_active_ = true;
    dsr_active_ = false;
}

std::uint8_t Uart8251::read(const std::uint8_t register_index) noexcept
{
    if((register_index & 0x01U) != 0) {
        return status();
    }

    const auto value = receive_buffer_;
    receive_ready_ = false;
    return value;
}

void Uart8251::write(const std::uint8_t register_index, const std::uint8_t value) noexcept
{
    if((register_index & 0x01U) != 0) {
        write_control(value);
    } else {
        transmit_buffer_ = static_cast<std::uint8_t>(
            value & static_cast<std::uint8_t>((1U << character_bits()) - 1U));
    }
}

void Uart8251::set_cts_active(const bool active) noexcept
{
    cts_active_ = active;
}

void Uart8251::set_dsr_active(const bool active) noexcept
{
    dsr_active_ = active;
}

void Uart8251::set_rxd(const bool high) noexcept
{
    rxd_high_ = high;
}

void Uart8251::set_sync_detect(const bool active) noexcept
{
    sync_detected_ = active;
    if(active) {
        sync_hunt_ = false;
    }
}

void Uart8251::clock_transmit(const std::uint64_t clock_edges) noexcept
{
    for(std::uint64_t edge = 0; edge < clock_edges; ++edge) {
        if((command_word_ & 0x08U) != 0) {
            txd_high_ = false;
            continue;
        }

        ++transmit_clock_count_;
        if(transmit_clock_count_ >= baud_factor()) {
            transmit_clock_count_ = 0;
            advance_transmitter();
        }
    }
}

void Uart8251::clock_receive(const std::uint64_t clock_edges) noexcept
{
    for(std::uint64_t edge = 0; edge < clock_edges; ++edge) {
        if(!queued_receive_active_ && !queued_receive_bytes_.empty()) {
            start_queued_receive_frame();
        }
        if(queued_receive_active_) {
            rxd_high_ = queued_receive_frame_[queued_receive_frame_index_];
        }
        receive_clock_edge();
        advance_queued_receive_line();
    }
}

void Uart8251::receive_byte(
    const std::uint8_t value,
    const bool parity_error,
    const bool framing_error) noexcept
{
    if(!receiver_enabled()) {
        return;
    }

    if(receive_ready_) {
        receive_overrun_error_ = true;
        return;
    }

    const auto mask = static_cast<std::uint8_t>((1U << character_bits()) - 1U);
    receive_buffer_ = static_cast<std::uint8_t>(value & mask);
    receive_ready_ = true;
    receive_parity_error_ = receive_parity_error_ || parity_error;
    receive_framing_error_ = receive_framing_error_ || framing_error;
    if(framing_error && receive_buffer_ == 0) {
        break_detected_ = true;
    }
}

bool Uart8251::queue_received_byte(const std::uint8_t value) noexcept
{
    if(queued_receive_bytes_.size() >= host_receive_queue_capacity) {
        return false;
    }
    queued_receive_bytes_.push_back(value);
    return true;
}

void Uart8251::clear_queued_receive_bytes() noexcept
{
    queued_receive_bytes_.clear();
    queued_receive_frame_size_ = 0;
    queued_receive_frame_index_ = 0;
    queued_receive_bit_clocks_ = 0;
    queued_receive_active_ = false;
    receive_phase_ = ReceivePhase::idle;
    receive_clock_count_ = 0;
    pending_receive_parity_error_ = false;
    rxd_high_ = true;
}

std::optional<std::uint8_t> Uart8251::take_transmitted_byte() noexcept
{
    if(transmitted_bytes_.empty()) {
        return std::nullopt;
    }
    const auto value = transmitted_bytes_.front();
    transmitted_bytes_.pop_front();
    return value;
}

std::size_t Uart8251::queued_receive_bytes() const noexcept
{
    return queued_receive_bytes_.size() + (queued_receive_active_ ? 1U : 0U);
}

std::uint8_t Uart8251::status() const noexcept
{
    std::uint8_t value = 0;
    if(tx_ready()) {
        value |= status_tx_ready;
    }
    if(receive_ready_) {
        value |= status_rx_ready;
    }
    if(!transmit_buffer_.has_value() && (!transmit_active_ || !transmit_payload_)) {
        value |= status_tx_empty;
    }
    if(receive_parity_error_) {
        value |= status_parity_error;
    }
    if(receive_overrun_error_) {
        value |= status_overrun_error;
    }
    if(receive_framing_error_) {
        value |= status_framing_error;
    }
    if(synchronous() ? sync_detected_ : break_detected_) {
        value |= status_sync_or_break;
    }
    if(dsr_active_) {
        value |= status_dsr;
    }
    return value;
}

std::uint8_t Uart8251::mode_word() const noexcept
{
    return mode_word_;
}

std::uint8_t Uart8251::command_word() const noexcept
{
    return command_word_;
}

bool Uart8251::tx_ready() const noexcept
{
    return transmitter_enabled() && cts_active_ && !transmit_buffer_.has_value();
}

bool Uart8251::rx_ready() const noexcept
{
    return receive_ready_;
}

bool Uart8251::txd() const noexcept
{
    return txd_high_;
}

bool Uart8251::interrupt_requested() const noexcept
{
    return tx_ready() || rx_ready();
}

void Uart8251::write_control(const std::uint8_t value) noexcept
{
    switch(initialization_phase_) {
    case InitializationPhase::mode:
        accept_mode(value);
        break;
    case InitializationPhase::sync1:
        sync_characters_[0] = value;
        sync_character_count_ = 1;
        initialization_phase_ = (mode_word_ & 0x80U) != 0
            ? InitializationPhase::command
            : InitializationPhase::sync2;
        break;
    case InitializationPhase::sync2:
        sync_characters_[1] = value;
        sync_character_count_ = 2;
        initialization_phase_ = InitializationPhase::command;
        break;
    case InitializationPhase::command:
        accept_command(value);
        break;
    }
}

void Uart8251::accept_mode(const std::uint8_t value) noexcept
{
    mode_word_ = value;
    command_word_ = 0;
    transmit_buffer_.reset();
    transmit_active_ = false;
    receive_ready_ = false;
    receive_phase_ = ReceivePhase::idle;
    sync_detected_ = false;
    sync_hunt_ = false;
    sync_character_count_ = 0;
    initialization_phase_ = synchronous()
        ? InitializationPhase::sync1
        : InitializationPhase::command;
}

void Uart8251::accept_command(const std::uint8_t value) noexcept
{
    if((value & 0x40U) != 0) {
        const auto cts = cts_active_;
        const auto dsr = dsr_active_;
        // Reset also clears the host serializer. Restore an idle line rather
        // than a possibly low bit from the discarded frame.
        const auto rxd = queued_receive_active_ ? true : rxd_high_;
        reset();
        cts_active_ = cts;
        dsr_active_ = dsr;
        rxd_high_ = rxd;
        return;
    }

    command_word_ = value;
    if((value & 0x10U) != 0) {
        receive_parity_error_ = false;
        receive_overrun_error_ = false;
        receive_framing_error_ = false;
        break_detected_ = false;
    }
    if(synchronous() && (value & 0x80U) != 0) {
        sync_hunt_ = true;
        sync_detected_ = false;
        sync_hunt_index_ = 0;
        receive_phase_ = ReceivePhase::sync_data;
        receive_bit_index_ = 0;
        receive_shift_ = 0;
    }
    if(!transmitter_enabled()) {
        txd_high_ = true;
    }
    if(!receiver_enabled()) {
        receive_phase_ = ReceivePhase::idle;
    }
}

void Uart8251::advance_transmitter() noexcept
{
    if(transmit_active_ && transmit_frame_index_ >= transmit_frame_size_) {
        if(transmit_payload_
           && transmitted_bytes_.size() < host_transmit_queue_capacity) {
            transmitted_bytes_.push_back(transmit_character_);
        }
        transmit_active_ = false;
    }

    if(!transmit_active_) {
        if(!transmitter_enabled() || !cts_active_) {
            txd_high_ = true;
            return;
        }

        if(transmit_buffer_.has_value()) {
            const auto value = *transmit_buffer_;
            transmit_buffer_.reset();
            build_transmit_frame(value, true);
        } else if(synchronous() && sync_character_count_ != 0) {
            const auto value = sync_characters_[sync_transmit_index_];
            sync_transmit_index_ = static_cast<std::uint8_t>(
                (sync_transmit_index_ + 1U) % sync_character_count_);
            build_transmit_frame(value, false);
        } else {
            txd_high_ = true;
            return;
        }
    }

    txd_high_ = transmit_frame_[transmit_frame_index_];
    ++transmit_frame_index_;
}

void Uart8251::build_transmit_frame(const std::uint8_t value, const bool payload) noexcept
{
    transmit_frame_size_ = 0;
    transmit_frame_index_ = 0;
    transmit_character_ = value;
    transmit_payload_ = payload;
    transmit_active_ = true;

    if(!synchronous()) {
        transmit_frame_[transmit_frame_size_++] = false;
    }

    std::uint8_t parity_ones = 0;
    for(std::uint8_t bit = 0; bit < character_bits(); ++bit) {
        const auto high = (value & static_cast<std::uint8_t>(1U << bit)) != 0;
        transmit_frame_[transmit_frame_size_++] = high;
        parity_ones = static_cast<std::uint8_t>(parity_ones + (high ? 1U : 0U));
    }

    if(parity_enabled()) {
        const auto parity_high = even_parity()
            ? (parity_ones & 1U) != 0
            : (parity_ones & 1U) == 0;
        transmit_frame_[transmit_frame_size_++] = parity_high;
    }

    if(!synchronous()) {
        const auto stop_code = static_cast<std::uint8_t>((mode_word_ >> 6U) & 0x03U);
        const auto stop_bits = stop_code <= 1 ? 1U : 2U;
        for(std::uint8_t bit = 0; bit < stop_bits; ++bit) {
            transmit_frame_[transmit_frame_size_++] = true;
        }
    }
}

void Uart8251::receive_clock_edge() noexcept
{
    if(!receiver_enabled()) {
        return;
    }

    if(synchronous()) {
        sample_sync_receive();
        return;
    }

    if(receive_phase_ == ReceivePhase::idle) {
        if(!rxd_high_) {
            receive_phase_ = ReceivePhase::start;
            receive_clock_count_ = baud_factor() > 1 ? baud_factor() / 2U : 1U;
        }
        return;
    }

    if(receive_clock_count_ > 1) {
        --receive_clock_count_;
        return;
    }
    receive_clock_count_ = baud_factor();
    sample_async_receive();
}

void Uart8251::sample_async_receive() noexcept
{
    switch(receive_phase_) {
    case ReceivePhase::start:
        if(rxd_high_) {
            receive_phase_ = ReceivePhase::idle;
            return;
        }
        receive_phase_ = ReceivePhase::data;
        receive_shift_ = 0;
        receive_bit_index_ = 0;
        receive_parity_ones_ = 0;
        pending_receive_parity_error_ = false;
        break;
    case ReceivePhase::data:
        if(rxd_high_) {
            receive_shift_ = static_cast<std::uint8_t>(
                receive_shift_ | static_cast<std::uint8_t>(1U << receive_bit_index_));
            ++receive_parity_ones_;
        }
        ++receive_bit_index_;
        if(receive_bit_index_ >= character_bits()) {
            receive_phase_ = parity_enabled() ? ReceivePhase::parity : ReceivePhase::stop;
        }
        break;
    case ReceivePhase::parity: {
        const auto expected_high = even_parity()
            ? (receive_parity_ones_ & 1U) != 0
            : (receive_parity_ones_ & 1U) == 0;
        pending_receive_parity_error_ = rxd_high_ != expected_high;
        receive_phase_ = ReceivePhase::stop;
        break;
    }
    case ReceivePhase::stop:
        receive_byte(receive_shift_, pending_receive_parity_error_, !rxd_high_);
        receive_phase_ = ReceivePhase::idle;
        break;
    default:
        receive_phase_ = ReceivePhase::idle;
        break;
    }
}

void Uart8251::sample_sync_receive() noexcept
{
    if(receive_phase_ != ReceivePhase::sync_data && receive_phase_ != ReceivePhase::sync_parity) {
        receive_phase_ = ReceivePhase::sync_data;
        receive_shift_ = 0;
        receive_bit_index_ = 0;
        receive_parity_ones_ = 0;
    }

    if(receive_phase_ == ReceivePhase::sync_data) {
        if(rxd_high_) {
            receive_shift_ = static_cast<std::uint8_t>(
                receive_shift_ | static_cast<std::uint8_t>(1U << receive_bit_index_));
            ++receive_parity_ones_;
        }
        ++receive_bit_index_;
        if(receive_bit_index_ >= character_bits()) {
            if(parity_enabled()) {
                receive_phase_ = ReceivePhase::sync_parity;
            } else {
                finish_sync_character();
            }
        }
        return;
    }

    const auto expected_high = even_parity()
        ? (receive_parity_ones_ & 1U) != 0
        : (receive_parity_ones_ & 1U) == 0;
    pending_receive_parity_error_ = rxd_high_ != expected_high;
    finish_sync_character();
}

void Uart8251::finish_sync_character() noexcept
{
    if(sync_hunt_ && sync_character_count_ != 0) {
        if(receive_shift_ == sync_characters_[sync_hunt_index_]) {
            ++sync_hunt_index_;
            if(sync_hunt_index_ >= sync_character_count_) {
                sync_detected_ = true;
                sync_hunt_ = false;
                sync_hunt_index_ = 0;
            }
        } else {
            sync_hunt_index_ = receive_shift_ == sync_characters_[0] ? 1 : 0;
        }
    } else {
        receive_byte(receive_shift_, pending_receive_parity_error_, false);
    }

    receive_phase_ = ReceivePhase::sync_data;
    receive_shift_ = 0;
    receive_bit_index_ = 0;
    receive_parity_ones_ = 0;
    pending_receive_parity_error_ = false;
}

void Uart8251::start_queued_receive_frame() noexcept
{
    const auto value = queued_receive_bytes_.front();
    queued_receive_bytes_.pop_front();
    queued_receive_frame_size_ = 0;
    queued_receive_frame_index_ = 0;

    if(!synchronous()) {
        queued_receive_frame_[queued_receive_frame_size_++] = false;
    }

    std::uint8_t parity_ones = 0;
    for(std::uint8_t bit = 0; bit < character_bits(); ++bit) {
        const auto high = (value & static_cast<std::uint8_t>(1U << bit)) != 0;
        queued_receive_frame_[queued_receive_frame_size_++] = high;
        parity_ones = static_cast<std::uint8_t>(parity_ones + (high ? 1U : 0U));
    }

    if(parity_enabled()) {
        const auto parity_high = even_parity()
            ? (parity_ones & 1U) != 0
            : (parity_ones & 1U) == 0;
        queued_receive_frame_[queued_receive_frame_size_++] = parity_high;
    }

    if(!synchronous()) {
        const auto stop_code = static_cast<std::uint8_t>((mode_word_ >> 6U) & 0x03U);
        const auto stop_bits = stop_code <= 1 ? 1U : 2U;
        for(std::uint8_t bit = 0; bit < stop_bits; ++bit) {
            queued_receive_frame_[queued_receive_frame_size_++] = true;
        }
    }

    queued_receive_bit_clocks_ = baud_factor();
    queued_receive_active_ = true;
}

void Uart8251::advance_queued_receive_line() noexcept
{
    if(!queued_receive_active_) {
        return;
    }
    if(queued_receive_bit_clocks_ > 1) {
        --queued_receive_bit_clocks_;
        return;
    }

    ++queued_receive_frame_index_;
    if(queued_receive_frame_index_ >= queued_receive_frame_size_) {
        queued_receive_active_ = false;
        queued_receive_frame_size_ = 0;
        queued_receive_frame_index_ = 0;
        queued_receive_bit_clocks_ = 0;
        rxd_high_ = true;
        return;
    }
    queued_receive_bit_clocks_ = baud_factor();
}

std::uint8_t Uart8251::character_bits() const noexcept
{
    return static_cast<std::uint8_t>(5U + ((mode_word_ >> 2U) & 0x03U));
}

std::uint32_t Uart8251::baud_factor() const noexcept
{
    switch(mode_word_ & 0x03U) {
    case 2:
        return 16;
    case 3:
        return 64;
    default:
        return 1;
    }
}

bool Uart8251::synchronous() const noexcept
{
    return (mode_word_ & 0x03U) == 0;
}

bool Uart8251::parity_enabled() const noexcept
{
    return (mode_word_ & 0x10U) != 0;
}

bool Uart8251::even_parity() const noexcept
{
    return (mode_word_ & 0x20U) != 0;
}

bool Uart8251::transmitter_enabled() const noexcept
{
    return initialization_phase_ == InitializationPhase::command && (command_word_ & 0x01U) != 0;
}

bool Uart8251::receiver_enabled() const noexcept
{
    return initialization_phase_ == InitializationPhase::command && (command_word_ & 0x04U) != 0;
}

} // namespace fk1
