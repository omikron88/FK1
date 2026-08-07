#include "fk1/interrupt_controller3214.hpp"

#include <algorithm>

namespace fk1 {

InterruptController3214::InterruptController3214() noexcept
{
    reset();
}

void InterruptController3214::reset() noexcept
{
    inputs_.fill(false);
    mask_ = 0;
}

void InterruptController3214::set_mask(const std::uint8_t enabled_inputs) noexcept
{
    mask_ = std::min(enabled_inputs, static_cast<std::uint8_t>(input_count));
}

void InterruptController3214::set_input(const std::size_t input_index, const bool active) noexcept
{
    if(input_index < inputs_.size()) {
        inputs_[input_index] = active;
    }
}

std::uint8_t InterruptController3214::mask() const noexcept
{
    return mask_;
}

bool InterruptController3214::input(const std::size_t input_index) const noexcept
{
    return input_index < inputs_.size() && inputs_[input_index];
}

bool InterruptController3214::pending() const noexcept
{
    return active_input() != input_count;
}

std::size_t InterruptController3214::active_input() const noexcept
{
    const auto first_enabled = input_count - static_cast<std::size_t>(mask_);
    for(auto input_index = input_count; input_index-- > first_enabled;) {
        if(inputs_[input_index]) {
            return input_index;
        }
    }
    return input_count;
}

std::uint8_t InterruptController3214::vector() const noexcept
{
    const auto input_index = active_input();
    if(input_index == input_count) {
        return no_vector;
    }
    return static_cast<std::uint8_t>((input_count - 1U - input_index) * 2U);
}

} // namespace fk1
