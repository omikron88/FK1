#include "fk1/ppi8255.hpp"

namespace fk1 {

Ppi8255::Ppi8255() noexcept
{
    reset();
}

void Ppi8255::reset() noexcept
{
    control_word_ = 0x9B;
    port_a_output_ = 0;
    port_b_output_ = 0;
    port_c_output_ = 0;
    port_a_input_ = 0;
    port_b_input_ = 0;
    port_c_input_ = 0;
    mode_set(control_word_);
}

std::uint8_t Ppi8255::read(const std::uint8_t register_index) noexcept
{
    switch(register_index & 0x03U) {
    case 0:
        return read_port_a();
    case 1:
        return read_port_b();
    case 2:
        return read_port_c();
    default:
        return 0xFF;
    }
}

void Ppi8255::write(const std::uint8_t register_index, const std::uint8_t value) noexcept
{
    switch(register_index & 0x03U) {
    case 0:
        port_a_output_ = value;
        if((group_a_mode_ == Mode::mode1 && !port_a_input_mode_)
           || group_a_mode_ == Mode::mode2) {
            obf_a_high_ = false;
            output_a_acknowledged_ = false;
            intr_a_ = false;
        }
        break;
    case 1:
        port_b_output_ = value;
        if(group_b_mode_ == Mode::mode1 && !port_b_input_mode_) {
            obf_b_high_ = false;
            output_b_acknowledged_ = false;
            intr_b_ = false;
        }
        break;
    case 2:
        port_c_output_ = value;
        break;
    default:
        if((value & 0x80U) != 0) {
            mode_set(value);
        } else {
            bit_set_reset(value);
        }
        break;
    }

    update_interrupts();
}

void Ppi8255::set_port_a_input(const std::uint8_t value) noexcept
{
    port_a_input_ = value;
}

void Ppi8255::set_port_b_input(const std::uint8_t value) noexcept
{
    port_b_input_ = value;
}

void Ppi8255::set_port_c_input(const std::uint8_t value) noexcept
{
    port_c_input_ = value;
}

void Ppi8255::set_strobe_a(const bool high) noexcept
{
    const auto falling = strobe_a_high_ && !high;
    strobe_a_high_ = high;
    if(falling && ((group_a_mode_ == Mode::mode1 && port_a_input_mode_)
                   || group_a_mode_ == Mode::mode2)) {
        port_a_input_latch_ = port_a_input_;
        ibf_a_ = true;
        intr_a_ = false;
    }
    update_interrupts();
}

void Ppi8255::set_strobe_b(const bool high) noexcept
{
    const auto falling = strobe_b_high_ && !high;
    strobe_b_high_ = high;
    if(falling && group_b_mode_ == Mode::mode1 && port_b_input_mode_) {
        port_b_input_latch_ = port_b_input_;
        ibf_b_ = true;
        intr_b_ = false;
    }
    update_interrupts();
}

void Ppi8255::set_ack_a(const bool high) noexcept
{
    const auto falling = ack_a_high_ && !high;
    const auto rising = !ack_a_high_ && high;
    ack_a_high_ = high;
    if(falling && ((group_a_mode_ == Mode::mode1 && !port_a_input_mode_)
                   || group_a_mode_ == Mode::mode2)) {
        obf_a_high_ = true;
    }
    if(rising) {
        output_a_acknowledged_ = true;
    }
    update_interrupts();
}

void Ppi8255::set_ack_b(const bool high) noexcept
{
    const auto falling = ack_b_high_ && !high;
    const auto rising = !ack_b_high_ && high;
    ack_b_high_ = high;
    if(falling && group_b_mode_ == Mode::mode1 && !port_b_input_mode_) {
        obf_b_high_ = true;
    }
    if(rising) {
        output_b_acknowledged_ = true;
    }
    update_interrupts();
}

void Ppi8255::pulse_strobe_a(const std::uint8_t value) noexcept
{
    set_port_a_input(value);
    set_strobe_a(false);
    set_strobe_a(true);
}

void Ppi8255::pulse_strobe_b(const std::uint8_t value) noexcept
{
    set_port_b_input(value);
    set_strobe_b(false);
    set_strobe_b(true);
}

void Ppi8255::pulse_ack_a() noexcept
{
    set_ack_a(false);
    set_ack_a(true);
}

void Ppi8255::pulse_ack_b() noexcept
{
    set_ack_b(false);
    set_ack_b(true);
}

std::uint8_t Ppi8255::output_a() const noexcept
{
    return port_a_output_;
}

std::uint8_t Ppi8255::output_b() const noexcept
{
    return port_b_output_;
}

std::uint8_t Ppi8255::output_c() const noexcept
{
    return port_c_output_;
}

std::uint8_t Ppi8255::control_word() const noexcept
{
    return control_word_;
}

Ppi8255::Mode Ppi8255::group_a_mode() const noexcept
{
    return group_a_mode_;
}

Ppi8255::Mode Ppi8255::group_b_mode() const noexcept
{
    return group_b_mode_;
}

bool Ppi8255::port_a_output_enabled() const noexcept
{
    return !port_a_input_mode_ || group_a_mode_ == Mode::mode2;
}

bool Ppi8255::port_b_output_enabled() const noexcept
{
    return !port_b_input_mode_;
}

bool Ppi8255::port_c_upper_output_enabled() const noexcept
{
    return !port_c_upper_input_;
}

bool Ppi8255::port_c_lower_output_enabled() const noexcept
{
    return !port_c_lower_input_;
}

bool Ppi8255::intr_a() const noexcept
{
    return intr_a_;
}

bool Ppi8255::intr_b() const noexcept
{
    return intr_b_;
}

void Ppi8255::mode_set(const std::uint8_t value) noexcept
{
    control_word_ = value;
    if((value & 0x40U) != 0) {
        group_a_mode_ = Mode::mode2;
    } else if((value & 0x20U) != 0) {
        group_a_mode_ = Mode::mode1;
    } else {
        group_a_mode_ = Mode::mode0;
    }
    group_b_mode_ = (value & 0x04U) != 0 ? Mode::mode1 : Mode::mode0;
    port_a_input_mode_ = (value & 0x10U) != 0;
    port_c_upper_input_ = (value & 0x08U) != 0;
    port_b_input_mode_ = (value & 0x02U) != 0;
    port_c_lower_input_ = (value & 0x01U) != 0;

    port_a_output_ = 0;
    port_b_output_ = 0;
    port_c_output_ = 0;
    port_a_input_latch_ = 0;
    port_b_input_latch_ = 0;
    strobe_a_high_ = true;
    strobe_b_high_ = true;
    ack_a_high_ = true;
    ack_b_high_ = true;
    ibf_a_ = false;
    ibf_b_ = false;
    obf_a_high_ = true;
    obf_b_high_ = true;
    output_a_acknowledged_ = false;
    output_b_acknowledged_ = false;
    inte_a_input_ = false;
    inte_a_output_ = false;
    inte_b_ = false;
    intr_a_ = false;
    intr_b_ = false;
}

void Ppi8255::bit_set_reset(const std::uint8_t value) noexcept
{
    const auto bit = static_cast<std::uint8_t>((value >> 1U) & 0x07U);
    const auto set = (value & 0x01U) != 0;

    if(bit == 4 && (group_a_mode_ == Mode::mode1 || group_a_mode_ == Mode::mode2)
       && (port_a_input_mode_ || group_a_mode_ == Mode::mode2)) {
        inte_a_input_ = set;
    } else if(bit == 6 && (group_a_mode_ == Mode::mode1 || group_a_mode_ == Mode::mode2)
              && (!port_a_input_mode_ || group_a_mode_ == Mode::mode2)) {
        inte_a_output_ = set;
    } else if(bit == 2 && group_b_mode_ == Mode::mode1) {
        inte_b_ = set;
    } else {
        const auto mask = static_cast<std::uint8_t>(1U << bit);
        if(set) {
            port_c_output_ = static_cast<std::uint8_t>(port_c_output_ | mask);
        } else {
            port_c_output_ = static_cast<std::uint8_t>(port_c_output_ & ~mask);
        }
    }
}

void Ppi8255::update_interrupts() noexcept
{
    const auto a_input_interrupt = (group_a_mode_ == Mode::mode1 || group_a_mode_ == Mode::mode2)
        && (port_a_input_mode_ || group_a_mode_ == Mode::mode2)
        && inte_a_input_ && ibf_a_ && strobe_a_high_;
    const auto a_output_interrupt = (group_a_mode_ == Mode::mode1 || group_a_mode_ == Mode::mode2)
        && (!port_a_input_mode_ || group_a_mode_ == Mode::mode2)
        && inte_a_output_ && output_a_acknowledged_ && obf_a_high_ && ack_a_high_;
    intr_a_ = a_input_interrupt || a_output_interrupt;

    if(group_b_mode_ != Mode::mode1) {
        intr_b_ = false;
    } else if(port_b_input_mode_) {
        intr_b_ = inte_b_ && ibf_b_ && strobe_b_high_;
    } else {
        intr_b_ = inte_b_ && output_b_acknowledged_ && obf_b_high_ && ack_b_high_;
    }
}

std::uint8_t Ppi8255::read_port_a() noexcept
{
    if(group_a_mode_ == Mode::mode1 || group_a_mode_ == Mode::mode2) {
        if(port_a_input_mode_ || group_a_mode_ == Mode::mode2) {
            const auto value = port_a_input_latch_;
            ibf_a_ = false;
            intr_a_ = false;
            update_interrupts();
            return value;
        }
    }
    return port_a_input_mode_ ? port_a_input_ : port_a_output_;
}

std::uint8_t Ppi8255::read_port_b() noexcept
{
    if(group_b_mode_ == Mode::mode1 && port_b_input_mode_) {
        const auto value = port_b_input_latch_;
        ibf_b_ = false;
        intr_b_ = false;
        update_interrupts();
        return value;
    }
    return port_b_input_mode_ ? port_b_input_ : port_b_output_;
}

std::uint8_t Ppi8255::read_port_c() const noexcept
{
    auto value = static_cast<std::uint8_t>(
        (port_c_upper_input_ ? port_c_input_ : port_c_output_) & 0xF0U);
    value = static_cast<std::uint8_t>(
        value | ((port_c_lower_input_ ? port_c_input_ : port_c_output_) & 0x0FU));

    const auto set_bit = [&value](const std::uint8_t bit, const bool high) {
        const auto mask = static_cast<std::uint8_t>(1U << bit);
        value = high ? static_cast<std::uint8_t>(value | mask)
                     : static_cast<std::uint8_t>(value & ~mask);
    };

    if(group_a_mode_ == Mode::mode1) {
        if(port_a_input_mode_) {
            set_bit(4, strobe_a_high_);
            set_bit(5, ibf_a_);
        } else {
            set_bit(7, obf_a_high_);
            set_bit(6, ack_a_high_);
        }
        set_bit(3, intr_a_);
    } else if(group_a_mode_ == Mode::mode2) {
        set_bit(7, obf_a_high_);
        set_bit(6, ack_a_high_);
        set_bit(5, ibf_a_);
        set_bit(4, strobe_a_high_);
        set_bit(3, intr_a_);
    }

    if(group_b_mode_ == Mode::mode1) {
        set_bit(2, port_b_input_mode_ ? strobe_b_high_ : ack_b_high_);
        set_bit(1, port_b_input_mode_ ? ibf_b_ : obf_b_high_);
        set_bit(0, intr_b_);
    }

    return value;
}

} // namespace fk1
