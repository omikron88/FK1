#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>

namespace fk1 {

class Uart8251 final {
public:
    static constexpr std::size_t host_receive_queue_capacity = 4'096;
    static constexpr std::size_t host_transmit_queue_capacity = 4'096;

    static constexpr std::uint8_t status_tx_ready = 0x01;
    static constexpr std::uint8_t status_rx_ready = 0x02;
    static constexpr std::uint8_t status_tx_empty = 0x04;
    static constexpr std::uint8_t status_parity_error = 0x08;
    static constexpr std::uint8_t status_overrun_error = 0x10;
    static constexpr std::uint8_t status_framing_error = 0x20;
    static constexpr std::uint8_t status_sync_or_break = 0x40;
    static constexpr std::uint8_t status_dsr = 0x80;

    Uart8251() noexcept;

    void reset() noexcept;
    [[nodiscard]] std::uint8_t read(std::uint8_t register_index) noexcept;
    void write(std::uint8_t register_index, std::uint8_t value) noexcept;

    void set_cts_active(bool active) noexcept;
    void set_dsr_active(bool active) noexcept;
    void set_rxd(bool high) noexcept;
    void set_sync_detect(bool active) noexcept;

    void clock_transmit(std::uint64_t clock_edges = 1) noexcept;
    void clock_receive(std::uint64_t clock_edges = 1) noexcept;
    void receive_byte(
        std::uint8_t value,
        bool parity_error = false,
        bool framing_error = false) noexcept;
    [[nodiscard]] bool queue_received_byte(std::uint8_t value) noexcept;
    void clear_queued_receive_bytes() noexcept;

    [[nodiscard]] std::optional<std::uint8_t> take_transmitted_byte() noexcept;
    [[nodiscard]] std::size_t queued_receive_bytes() const noexcept;
    [[nodiscard]] std::uint8_t status() const noexcept;
    [[nodiscard]] std::uint8_t mode_word() const noexcept;
    [[nodiscard]] std::uint8_t command_word() const noexcept;
    [[nodiscard]] bool tx_ready() const noexcept;
    [[nodiscard]] bool rx_ready() const noexcept;
    [[nodiscard]] bool txd() const noexcept;
    [[nodiscard]] bool interrupt_requested() const noexcept;

private:
    enum class InitializationPhase {
        mode,
        sync1,
        sync2,
        command,
    };

    enum class ReceivePhase {
        idle,
        start,
        data,
        parity,
        stop,
        sync_data,
        sync_parity,
    };

    void write_control(std::uint8_t value) noexcept;
    void accept_mode(std::uint8_t value) noexcept;
    void accept_command(std::uint8_t value) noexcept;
    void advance_transmitter() noexcept;
    void build_transmit_frame(std::uint8_t value, bool payload) noexcept;
    void receive_clock_edge() noexcept;
    void sample_async_receive() noexcept;
    void sample_sync_receive() noexcept;
    void finish_sync_character() noexcept;
    void start_queued_receive_frame() noexcept;
    void advance_queued_receive_line() noexcept;
    [[nodiscard]] std::uint8_t character_bits() const noexcept;
    [[nodiscard]] std::uint32_t baud_factor() const noexcept;
    [[nodiscard]] bool synchronous() const noexcept;
    [[nodiscard]] bool parity_enabled() const noexcept;
    [[nodiscard]] bool even_parity() const noexcept;
    [[nodiscard]] bool transmitter_enabled() const noexcept;
    [[nodiscard]] bool receiver_enabled() const noexcept;

    InitializationPhase initialization_phase_{InitializationPhase::mode};
    ReceivePhase receive_phase_{ReceivePhase::idle};
    std::uint8_t mode_word_{0};
    std::uint8_t command_word_{0};
    std::array<std::uint8_t, 2> sync_characters_{};
    std::uint8_t sync_character_count_{0};
    std::uint8_t sync_transmit_index_{0};
    std::uint8_t sync_hunt_index_{0};

    std::optional<std::uint8_t> transmit_buffer_;
    std::array<bool, 16> transmit_frame_{};
    std::size_t transmit_frame_size_{0};
    std::size_t transmit_frame_index_{0};
    std::uint8_t transmit_character_{0};
    bool transmit_active_{false};
    bool transmit_payload_{false};
    bool txd_high_{true};
    std::uint32_t transmit_clock_count_{0};
    std::deque<std::uint8_t> transmitted_bytes_;

    std::uint8_t receive_buffer_{0};
    std::uint8_t receive_shift_{0};
    std::uint8_t receive_bit_index_{0};
    std::uint8_t receive_parity_ones_{0};
    std::uint32_t receive_clock_count_{0};
    bool receive_ready_{false};
    bool receive_parity_error_{false};
    bool receive_overrun_error_{false};
    bool receive_framing_error_{false};
    bool pending_receive_parity_error_{false};
    bool rxd_high_{true};
    bool break_detected_{false};
    bool sync_detected_{false};
    bool sync_hunt_{false};

    std::deque<std::uint8_t> queued_receive_bytes_;
    std::array<bool, 16> queued_receive_frame_{};
    std::size_t queued_receive_frame_size_{0};
    std::size_t queued_receive_frame_index_{0};
    std::uint32_t queued_receive_bit_clocks_{0};
    bool queued_receive_active_{false};

    // FK-1 diagnostics transmit without an external modem attached. The board
    // therefore needs CTS asserted; keep it a named, externally replaceable pin.
    bool cts_active_{true};
    bool dsr_active_{false};
};

} // namespace fk1
