#pragma once

#include <cstdint>

namespace fk1 {

class Ppi8255 final {
public:
    enum class Mode : std::uint8_t {
        mode0,
        mode1,
        mode2,
    };

    Ppi8255() noexcept;

    void reset() noexcept;

    [[nodiscard]] std::uint8_t read(std::uint8_t register_index) noexcept;
    void write(std::uint8_t register_index, std::uint8_t value) noexcept;

    void set_port_a_input(std::uint8_t value) noexcept;
    void set_port_b_input(std::uint8_t value) noexcept;
    void set_port_c_input(std::uint8_t value) noexcept;

    void set_strobe_a(bool high) noexcept;
    void set_strobe_b(bool high) noexcept;
    void set_ack_a(bool high) noexcept;
    void set_ack_b(bool high) noexcept;

    void pulse_strobe_a(std::uint8_t value) noexcept;
    void pulse_strobe_b(std::uint8_t value) noexcept;
    void pulse_ack_a() noexcept;
    void pulse_ack_b() noexcept;

    [[nodiscard]] std::uint8_t output_a() const noexcept;
    [[nodiscard]] std::uint8_t output_b() const noexcept;
    [[nodiscard]] std::uint8_t output_c() const noexcept;
    [[nodiscard]] std::uint8_t control_word() const noexcept;

    [[nodiscard]] Mode group_a_mode() const noexcept;
    [[nodiscard]] Mode group_b_mode() const noexcept;
    [[nodiscard]] bool port_a_output_enabled() const noexcept;
    [[nodiscard]] bool port_b_output_enabled() const noexcept;
    [[nodiscard]] bool port_c_upper_output_enabled() const noexcept;
    [[nodiscard]] bool port_c_lower_output_enabled() const noexcept;
    [[nodiscard]] bool intr_a() const noexcept;
    [[nodiscard]] bool intr_b() const noexcept;

private:
    void mode_set(std::uint8_t value) noexcept;
    void bit_set_reset(std::uint8_t value) noexcept;
    void update_interrupts() noexcept;
    [[nodiscard]] std::uint8_t read_port_a() noexcept;
    [[nodiscard]] std::uint8_t read_port_b() noexcept;
    [[nodiscard]] std::uint8_t read_port_c() const noexcept;

    std::uint8_t control_word_{0x9B};
    std::uint8_t port_a_output_{0};
    std::uint8_t port_b_output_{0};
    std::uint8_t port_c_output_{0};
    std::uint8_t port_a_input_{0};
    std::uint8_t port_b_input_{0};
    std::uint8_t port_c_input_{0};
    std::uint8_t port_a_input_latch_{0};
    std::uint8_t port_b_input_latch_{0};

    Mode group_a_mode_{Mode::mode0};
    Mode group_b_mode_{Mode::mode0};
    bool port_a_input_mode_{true};
    bool port_b_input_mode_{true};
    bool port_c_upper_input_{true};
    bool port_c_lower_input_{true};

    bool strobe_a_high_{true};
    bool strobe_b_high_{true};
    bool ack_a_high_{true};
    bool ack_b_high_{true};
    bool ibf_a_{false};
    bool ibf_b_{false};
    bool obf_a_high_{true};
    bool obf_b_high_{true};
    bool output_a_acknowledged_{false};
    bool output_b_acknowledged_{false};
    bool inte_a_input_{false};
    bool inte_a_output_{false};
    bool inte_b_{false};
    bool intr_a_{false};
    bool intr_b_{false};
};

} // namespace fk1
