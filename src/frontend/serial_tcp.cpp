#include "serial_tcp.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <deque>
#include <string>
#include <system_error>
#include <utility>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace fk1::frontend {
namespace {

#ifdef _WIN32
using NativeSocket = SOCKET;
using SocketLength = int;
constexpr auto invalid_socket = INVALID_SOCKET;

[[nodiscard]] int last_socket_error() noexcept
{
    return WSAGetLastError();
}

void close_socket(const NativeSocket socket) noexcept
{
    if(socket != invalid_socket) {
        closesocket(socket);
    }
}

[[nodiscard]] bool set_nonblocking(const NativeSocket socket) noexcept
{
    auto enabled = u_long{1};
    // MinGW declares FIONBIO as unsigned long, while ioctlsocket takes a
    // signed long command. The bit pattern is the Winsock ABI value; make the
    // intentional conversion explicit instead of triggering -Wsign-conversion.
    return ioctlsocket(socket, static_cast<long>(FIONBIO), &enabled) == 0;
}

[[nodiscard]] bool would_block(const int error) noexcept
{
    return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS;
}

constexpr auto send_flags = 0;
#else
using NativeSocket = int;
using SocketLength = socklen_t;
constexpr auto invalid_socket = -1;

[[nodiscard]] int last_socket_error() noexcept
{
    return errno;
}

void close_socket(const NativeSocket socket) noexcept
{
    if(socket != invalid_socket) {
        close(socket);
    }
}

[[nodiscard]] bool set_nonblocking(const NativeSocket socket) noexcept
{
    const auto flags = fcntl(socket, F_GETFL, 0);
    return flags >= 0 && fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
}

[[nodiscard]] bool would_block(const int error) noexcept
{
    return error == EWOULDBLOCK || error == EAGAIN || error == EINPROGRESS;
}

#ifdef MSG_NOSIGNAL
constexpr auto send_flags = MSG_NOSIGNAL;
#else
constexpr auto send_flags = 0;
#endif
#endif

[[nodiscard]] std::string endpoint_text(const TcpEndpoint& endpoint)
{
    if(endpoint.host.find(':') != std::string::npos) {
        return '[' + endpoint.host + "]:" + std::to_string(endpoint.port);
    }
    return endpoint.host + ':' + std::to_string(endpoint.port);
}

void set_no_delay(const NativeSocket socket) noexcept
{
    const auto enabled = 1;
#ifdef _WIN32
    static_cast<void>(setsockopt(
        socket,
        IPPROTO_TCP,
        TCP_NODELAY,
        reinterpret_cast<const char*>(&enabled),
        static_cast<int>(sizeof(enabled))));
#else
    static_cast<void>(setsockopt(
        socket,
        IPPROTO_TCP,
        TCP_NODELAY,
        &enabled,
        static_cast<socklen_t>(sizeof(enabled))));
#endif
}

} // namespace

std::optional<TcpEndpoint> parse_tcp_endpoint(
    const std::string_view text,
    std::string& error)
{
    std::string_view host;
    std::string_view port_text;
    if(text.starts_with('[')) {
        const auto closing = text.find(']');
        if(closing == std::string_view::npos || closing + 1U >= text.size()
           || text[closing + 1U] != ':') {
            error = "TCP endpoint must use [address]:port for IPv6";
            return std::nullopt;
        }
        host = text.substr(1, closing - 1U);
        port_text = text.substr(closing + 2U);
    } else {
        const auto separator = text.rfind(':');
        if(separator == std::string_view::npos) {
            error = "TCP endpoint must use host:port";
            return std::nullopt;
        }
        host = text.substr(0, separator);
        port_text = text.substr(separator + 1U);
        if(host.find(':') != std::string_view::npos) {
            error = "TCP endpoint must use [address]:port for IPv6";
            return std::nullopt;
        }
    }

    std::uint32_t port = 0;
    const auto parsed = std::from_chars(
        port_text.data(),
        port_text.data() + port_text.size(),
        port);
    if(host.empty() || port_text.empty() || parsed.ec != std::errc{}
       || parsed.ptr != port_text.data() + port_text.size()
       || port == 0 || port > 65'535U) {
        error = "TCP endpoint requires a host and port from 1 through 65535";
        return std::nullopt;
    }
    return TcpEndpoint{std::string{host}, static_cast<std::uint16_t>(port)};
}

struct TcpSerialLink::Impl {
    enum class State {
        stopped,
        listening,
        reconnect_wait,
        connecting,
        connected,
    };

    ~Impl()
    {
        close_socket(peer_socket);
        close_socket(listener_socket);
#ifdef _WIN32
        if(winsock_started) {
            WSACleanup();
        }
#endif
    }

    [[nodiscard]] bool initialize_network(std::string& error)
    {
#ifdef _WIN32
        if(!winsock_started) {
            WSADATA data{};
            const auto result = WSAStartup(MAKEWORD(2, 2), &data);
            if(result != 0) {
                error = "WSAStartup failed with error " + std::to_string(result);
                return false;
            }
            winsock_started = true;
        }
#else
        static_cast<void>(error);
#endif
        return true;
    }

    [[nodiscard]] bool resolve(
        const bool passive,
        addrinfo*& addresses,
        std::string& error) const
    {
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        hints.ai_flags = passive ? AI_PASSIVE : 0;
        const auto service = std::to_string(endpoint.port);
        const auto result = getaddrinfo(
            endpoint.host.c_str(),
            service.c_str(),
            &hints,
            &addresses);
        if(result != 0) {
#ifdef _WIN32
            error = "cannot resolve TCP endpoint " + endpoint_text(endpoint)
                + " (error " + std::to_string(result) + ')';
#else
            error = "cannot resolve TCP endpoint " + endpoint_text(endpoint)
                + ": " + gai_strerror(result);
#endif
            return false;
        }
        return true;
    }

    [[nodiscard]] bool open_listener(std::string& error)
    {
        addrinfo* addresses = nullptr;
        if(!resolve(true, addresses, error)) {
            return false;
        }

        for(auto* address = addresses; address != nullptr; address = address->ai_next) {
            const auto candidate = socket(
                address->ai_family,
                address->ai_socktype,
                address->ai_protocol);
            if(candidate == invalid_socket) {
                continue;
            }

            const auto reuse = 1;
#ifdef _WIN32
            static_cast<void>(setsockopt(
                candidate,
                SOL_SOCKET,
                SO_REUSEADDR,
                reinterpret_cast<const char*>(&reuse),
                static_cast<int>(sizeof(reuse))));
#else
            static_cast<void>(setsockopt(
                candidate,
                SOL_SOCKET,
                SO_REUSEADDR,
                &reuse,
                static_cast<socklen_t>(sizeof(reuse))));
#endif
            if(bind(
                   candidate,
                   address->ai_addr,
                   static_cast<SocketLength>(address->ai_addrlen)) == 0
               && listen(candidate, 1) == 0
               && set_nonblocking(candidate)) {
                listener_socket = candidate;
                freeaddrinfo(addresses);
                state = State::listening;
                messages.push_back("Serial listening on " + endpoint_text(endpoint));
                return true;
            }
            close_socket(candidate);
        }
        freeaddrinfo(addresses);
        error = "cannot listen on TCP endpoint " + endpoint_text(endpoint)
            + " (socket error " + std::to_string(last_socket_error()) + ')';
        return false;
    }

    [[nodiscard]] bool begin_connect(std::string& error)
    {
        addrinfo* addresses = nullptr;
        if(!resolve(false, addresses, error)) {
            return false;
        }

        auto last_error = 0;
        for(auto* address = addresses; address != nullptr; address = address->ai_next) {
            const auto candidate = socket(
                address->ai_family,
                address->ai_socktype,
                address->ai_protocol);
            if(candidate == invalid_socket) {
                last_error = last_socket_error();
                continue;
            }
            if(!set_nonblocking(candidate)) {
                last_error = last_socket_error();
                close_socket(candidate);
                continue;
            }

            const auto result = connect(
                candidate,
                address->ai_addr,
                static_cast<SocketLength>(address->ai_addrlen));
            if(result == 0) {
                peer_socket = candidate;
                freeaddrinfo(addresses);
                mark_connected();
                return true;
            }
            last_error = last_socket_error();
            if(would_block(last_error)) {
                peer_socket = candidate;
                freeaddrinfo(addresses);
                state = State::connecting;
                return true;
            }
            close_socket(candidate);
        }
        freeaddrinfo(addresses);
        schedule_reconnect("Serial connect failed (socket error "
            + std::to_string(last_error) + "); retrying");
        return true;
    }

    void mark_connected()
    {
        set_no_delay(peer_socket);
        incoming.clear();
        outgoing.clear();
        state = State::connected;
        messages.push_back("Serial link connected");
    }

    void schedule_reconnect(std::string message)
    {
        close_socket(peer_socket);
        peer_socket = invalid_socket;
        incoming.clear();
        outgoing.clear();
        state = State::reconnect_wait;
        retry_at = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        messages.push_back(std::move(message));
    }

    void disconnect_peer()
    {
        close_socket(peer_socket);
        peer_socket = invalid_socket;
        incoming.clear();
        outgoing.clear();
        messages.push_back("Serial link disconnected");
        if(mode == TcpSerialMode::listen) {
            state = State::listening;
        } else {
            state = State::reconnect_wait;
            retry_at = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        }
    }

    void poll_listener()
    {
        const auto accepted = accept(listener_socket, nullptr, nullptr);
        if(accepted == invalid_socket) {
            return;
        }
        if(!set_nonblocking(accepted)) {
            close_socket(accepted);
            return;
        }
        peer_socket = accepted;
        mark_connected();
    }

    void poll_connecting()
    {
        fd_set write_set;
        fd_set error_set;
        FD_ZERO(&write_set);
        FD_ZERO(&error_set);
        FD_SET(peer_socket, &write_set);
        FD_SET(peer_socket, &error_set);
        timeval timeout{};
#ifdef _WIN32
        const auto selected = select(0, nullptr, &write_set, &error_set, &timeout);
#else
        const auto selected = select(peer_socket + 1, nullptr, &write_set, &error_set, &timeout);
#endif
        if(selected <= 0) {
            return;
        }

        auto error = 0;
#ifdef _WIN32
        auto length = static_cast<int>(sizeof(error));
        const auto result = getsockopt(
            peer_socket,
            SOL_SOCKET,
            SO_ERROR,
            reinterpret_cast<char*>(&error),
            &length);
#else
        auto length = static_cast<socklen_t>(sizeof(error));
        const auto result = getsockopt(peer_socket, SOL_SOCKET, SO_ERROR, &error, &length);
#endif
        if(result == 0 && error == 0) {
            mark_connected();
            return;
        }
        schedule_reconnect("Serial connect failed (socket error "
            + std::to_string(error) + "); retrying");
    }

    void send_pending()
    {
        std::array<std::uint8_t, 1'024> buffer{};
        while(!outgoing.empty()) {
            const auto count = std::min(buffer.size(), outgoing.size());
            for(std::size_t index = 0; index < count; ++index) {
                buffer[index] = outgoing[index];
            }
#ifdef _WIN32
            const auto sent = send(
                peer_socket,
                reinterpret_cast<const char*>(buffer.data()),
                static_cast<int>(count),
                send_flags);
#else
            const auto sent = send(peer_socket, buffer.data(), count, send_flags);
#endif
            if(sent > 0) {
                for(std::size_t index = 0; index < static_cast<std::size_t>(sent); ++index) {
                    outgoing.pop_front();
                }
                continue;
            }
            if(sent < 0 && would_block(last_socket_error())) {
                return;
            }
            disconnect_peer();
            return;
        }
    }

    void receive_pending()
    {
        std::array<std::uint8_t, 1'024> buffer{};
        while(incoming.size() < TcpSerialLink::queue_capacity) {
            const auto capacity = std::min(
                buffer.size(),
                TcpSerialLink::queue_capacity - incoming.size());
#ifdef _WIN32
            const auto received = recv(
                peer_socket,
                reinterpret_cast<char*>(buffer.data()),
                static_cast<int>(capacity),
                0);
#else
            const auto received = recv(peer_socket, buffer.data(), capacity, 0);
#endif
            if(received > 0) {
                for(std::size_t index = 0; index < static_cast<std::size_t>(received); ++index) {
                    incoming.push_back(buffer[index]);
                }
                continue;
            }
            if(received < 0 && would_block(last_socket_error())) {
                return;
            }
            disconnect_peer();
            return;
        }
    }

    void poll()
    {
        switch(state) {
        case State::listening:
            poll_listener();
            break;
        case State::reconnect_wait:
            if(std::chrono::steady_clock::now() >= retry_at) {
                std::string error;
                if(!begin_connect(error)) {
                    schedule_reconnect(error + "; retrying");
                }
            }
            break;
        case State::connecting:
            poll_connecting();
            break;
        case State::connected:
            send_pending();
            if(state == State::connected) {
                receive_pending();
            }
            break;
        case State::stopped:
            break;
        }
    }

    TcpSerialMode mode{TcpSerialMode::listen};
    TcpEndpoint endpoint;
    State state{State::stopped};
    NativeSocket listener_socket{invalid_socket};
    NativeSocket peer_socket{invalid_socket};
    std::chrono::steady_clock::time_point retry_at{};
    std::deque<std::uint8_t> incoming;
    std::deque<std::uint8_t> outgoing;
    std::deque<std::string> messages;
#ifdef _WIN32
    bool winsock_started{false};
#endif
};

TcpSerialLink::TcpSerialLink()
    : impl_{std::make_unique<Impl>()}
{
}

TcpSerialLink::~TcpSerialLink() = default;

bool TcpSerialLink::start(
    const TcpSerialMode mode,
    TcpEndpoint endpoint,
    std::string& error)
{
    if(impl_->state != Impl::State::stopped) {
        error = "TCP serial link is already started";
        return false;
    }
    if(!impl_->initialize_network(error)) {
        return false;
    }
    impl_->mode = mode;
    impl_->endpoint = std::move(endpoint);
    if(mode == TcpSerialMode::listen) {
        return impl_->open_listener(error);
    }
    impl_->messages.push_back("Serial connecting to " + endpoint_text(impl_->endpoint));
    return impl_->begin_connect(error);
}

void TcpSerialLink::poll()
{
    impl_->poll();
}

bool TcpSerialLink::connected() const noexcept
{
    return impl_->state == Impl::State::connected;
}

bool TcpSerialLink::queue_transmit(const std::uint8_t value)
{
    if(!connected() || impl_->outgoing.size() >= queue_capacity) {
        return false;
    }
    impl_->outgoing.push_back(value);
    return true;
}

std::optional<std::uint8_t> TcpSerialLink::take_received_byte() noexcept
{
    if(impl_->incoming.empty()) {
        return std::nullopt;
    }
    const auto value = impl_->incoming.front();
    impl_->incoming.pop_front();
    return value;
}

std::optional<std::string> TcpSerialLink::take_status_message()
{
    if(impl_->messages.empty()) {
        return std::nullopt;
    }
    auto message = std::move(impl_->messages.front());
    impl_->messages.pop_front();
    return message;
}

} // namespace fk1::frontend
