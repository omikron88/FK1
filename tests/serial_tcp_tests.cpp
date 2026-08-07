#include "serial_tcp.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <thread>

namespace {

int failures = 0;

void expect(const bool condition, const char* message)
{
    if(!condition) {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

void test_endpoint_parser()
{
    std::string error;
    const auto ipv4 = fk1::frontend::parse_tcp_endpoint("127.0.0.1:2025", error);
    expect(ipv4 && ipv4->host == "127.0.0.1" && ipv4->port == 2025,
           "the TCP serial parser must accept an IPv4 host and port");

    error.clear();
    const auto ipv6 = fk1::frontend::parse_tcp_endpoint("[::1]:65535", error);
    expect(ipv6 && ipv6->host == "::1" && ipv6->port == 65'535,
           "the TCP serial parser must accept a bracketed IPv6 address");

    error.clear();
    expect(!fk1::frontend::parse_tcp_endpoint("127.0.0.1:0", error),
           "the TCP serial parser must reject port zero");
    error.clear();
    expect(!fk1::frontend::parse_tcp_endpoint("::1:2025", error),
           "the TCP serial parser must require IPv6 brackets");
}

void test_loopback_link()
{
    fk1::frontend::TcpSerialLink listener;
    std::optional<fk1::frontend::TcpEndpoint> endpoint;
    std::string error;
    for(std::uint16_t offset = 0; offset < 64; ++offset) {
        const auto port = static_cast<std::uint16_t>(44'000U + offset);
        fk1::frontend::TcpEndpoint candidate{"127.0.0.1", port};
        error.clear();
        if(listener.start(fk1::frontend::TcpSerialMode::listen, candidate, error)) {
            endpoint = std::move(candidate);
            break;
        }
    }
    expect(endpoint.has_value(), "the TCP serial listener must bind a loopback port");
    if(!endpoint) {
        return;
    }

    fk1::frontend::TcpSerialLink client;
    error.clear();
    expect(client.start(fk1::frontend::TcpSerialMode::connect, *endpoint, error),
           "the TCP serial client must start a non-blocking connection");

    const auto connect_deadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(3);
    while((!listener.connected() || !client.connected())
          && std::chrono::steady_clock::now() < connect_deadline) {
        listener.poll();
        client.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    expect(listener.connected() && client.connected(),
           "the two TCP serial endpoints must establish a loopback connection");
    if(!listener.connected() || !client.connected()) {
        return;
    }

    expect(client.queue_transmit(0xA5),
           "a connected TCP serial client must accept outgoing data");
    std::optional<std::uint8_t> received_by_listener;
    const auto first_deadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(3);
    while(!received_by_listener && std::chrono::steady_clock::now() < first_deadline) {
        client.poll();
        listener.poll();
        received_by_listener = listener.take_received_byte();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    expect(received_by_listener && *received_by_listener == 0xA5,
           "raw TCP serial data must travel from client to listener unchanged");

    expect(listener.queue_transmit(0x5A),
           "a connected TCP serial listener must accept outgoing data");
    std::optional<std::uint8_t> received_by_client;
    const auto second_deadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(3);
    while(!received_by_client && std::chrono::steady_clock::now() < second_deadline) {
        listener.poll();
        client.poll();
        received_by_client = client.take_received_byte();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    expect(received_by_client && *received_by_client == 0x5A,
           "raw TCP serial data must travel from listener to client unchanged");
}

} // namespace

int main()
{
    test_endpoint_parser();
    test_loopback_link();

    if(failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All FK-1 TCP serial tests passed\n";
    return 0;
}
