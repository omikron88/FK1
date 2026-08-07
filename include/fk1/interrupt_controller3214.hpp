#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace fk1 {

class InterruptController3214 final {
public:
    static constexpr std::size_t input_count = 8;
    static constexpr std::uint8_t no_vector = 0xFF;

    InterruptController3214() noexcept;

    void reset() noexcept;
    void set_mask(std::uint8_t enabled_inputs) noexcept;
    void set_input(std::size_t input, bool active) noexcept;

    [[nodiscard]] std::uint8_t mask() const noexcept;
    [[nodiscard]] bool input(std::size_t input) const noexcept;
    [[nodiscard]] bool pending() const noexcept;
    [[nodiscard]] std::size_t active_input() const noexcept;
    [[nodiscard]] std::uint8_t vector() const noexcept;

private:
    std::array<bool, input_count> inputs_{};
    std::uint8_t mask_{0};
};

} // namespace fk1
