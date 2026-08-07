#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace fk1::frontend {

struct TcpEndpoint {
    std::string host;
    std::uint16_t port{0};
};

enum class TcpSerialMode {
    listen,
    connect,
};

[[nodiscard]] std::optional<TcpEndpoint> parse_tcp_endpoint(
    std::string_view text,
    std::string& error);

class TcpSerialLink final {
public:
    static constexpr std::size_t queue_capacity = 4'096;

    TcpSerialLink();
    ~TcpSerialLink();

    TcpSerialLink(const TcpSerialLink&) = delete;
    TcpSerialLink& operator=(const TcpSerialLink&) = delete;
    TcpSerialLink(TcpSerialLink&&) = delete;
    TcpSerialLink& operator=(TcpSerialLink&&) = delete;

    [[nodiscard]] bool start(
        TcpSerialMode mode,
        TcpEndpoint endpoint,
        std::string& error);
    void poll();

    [[nodiscard]] bool connected() const noexcept;
    [[nodiscard]] bool queue_transmit(std::uint8_t value);
    [[nodiscard]] std::optional<std::uint8_t> take_received_byte() noexcept;
    [[nodiscard]] std::optional<std::string> take_status_message();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace fk1::frontend
