#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
static constexpr socket_t BAD_SOCKET = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
using socket_t = int;
static constexpr socket_t BAD_SOCKET = -1;
#endif

extern "C" {
#include "protocol.h"
#include "stream_parser.h"
}

static constexpr const char* GATEWAY_IP = "192.168.77.2";
static constexpr uint16_t GATEWAY_PORT = 5000u;
static constexpr uint32_t CONNECT_RETRY_MS = 50u;
static constexpr uint32_t CONNECT_ATTEMPT_TIMEOUT_MS = 50u;
static constexpr uint32_t PING_PERIOD_MS = 100u;
static constexpr uint32_t PONG_TIMEOUT_MS = 200u;

static volatile std::sig_atomic_t g_stop_requested = 0;

enum class WaitResult
{
    Pong,
    Timeout,
    Error,
    Stopped
};

static void handle_interrupt(int)
{
    g_stop_requested = 1;
}

static bool stop_requested()
{
    return g_stop_requested != 0;
}

static bool net_init()
{
#ifdef _WIN32
    WSADATA wsa{};
    return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
#else
    return true;
#endif
}

static void net_cleanup()
{
#ifdef _WIN32
    WSACleanup();
#endif
}

static void close_sock(socket_t socket)
{
#ifdef _WIN32
    if (socket != BAD_SOCKET)
    {
        closesocket(socket);
    }
#else
    if (socket != BAD_SOCKET)
    {
        close(socket);
    }
#endif
}

static bool set_receive_timeout(socket_t socket, uint32_t timeout_ms)
{
#ifdef _WIN32
    const DWORD timeout = static_cast<DWORD>(timeout_ms);
    return setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
                      reinterpret_cast<const char*>(&timeout),
                      sizeof(timeout)) == 0;
#else
    timeval timeout{};
    timeout.tv_sec = static_cast<time_t>(timeout_ms / 1000u);
    timeout.tv_usec = static_cast<suseconds_t>((timeout_ms % 1000u) * 1000u);
    return setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
                      &timeout, sizeof(timeout)) == 0;
#endif
}

static bool set_nonblocking(socket_t socket, bool enabled)
{
#ifdef _WIN32
    u_long mode = enabled ? 1UL : 0UL;
    return ioctlsocket(socket, FIONBIO, &mode) == 0;
#else
    const int flags = fcntl(socket, F_GETFL, 0);
    if (flags < 0)
    {
        return false;
    }

    const int updated_flags = enabled ? (flags | O_NONBLOCK) :
                                        (flags & ~O_NONBLOCK);
    return fcntl(socket, F_SETFL, updated_flags) == 0;
#endif
}

static bool connect_in_progress()
{
#ifdef _WIN32
    const int error = WSAGetLastError();
    return (error == WSAEWOULDBLOCK) ||
           (error == WSAEINPROGRESS) ||
           (error == WSAEALREADY);
#else
    return errno == EINPROGRESS;
#endif
}

static int wait_until_writable(socket_t socket, uint32_t timeout_ms)
{
    fd_set write_set;
    fd_set error_set;
    FD_ZERO(&write_set);
    FD_ZERO(&error_set);
    FD_SET(socket, &write_set);
    FD_SET(socket, &error_set);

    timeval timeout{};
    timeout.tv_sec = static_cast<long>(timeout_ms / 1000u);
    timeout.tv_usec = static_cast<long>((timeout_ms % 1000u) * 1000u);

#ifdef _WIN32
    return select(0, nullptr, &write_set, &error_set, &timeout);
#else
    return select(socket + 1, nullptr, &write_set, &error_set, &timeout);
#endif
}

static bool connect_with_timeout(
    socket_t socket,
    const sockaddr_in& address,
    uint32_t timeout_ms)
{
    if (!set_nonblocking(socket, true))
    {
        return false;
    }

    bool connected = false;
    const int result = connect(
        socket,
        reinterpret_cast<const sockaddr*>(&address),
        sizeof(address));

    if (result == 0)
    {
        connected = true;
    }
    else if (connect_in_progress())
    {
        const int ready = wait_until_writable(socket, timeout_ms);
        if (ready > 0)
        {
            int socket_error = 0;
#ifdef _WIN32
            int option_length = static_cast<int>(sizeof(socket_error));
            const int option_result = getsockopt(
                socket,
                SOL_SOCKET,
                SO_ERROR,
                reinterpret_cast<char*>(&socket_error),
                &option_length);
#else
            socklen_t option_length = sizeof(socket_error);
            const int option_result = getsockopt(
                socket, SOL_SOCKET, SO_ERROR, &socket_error, &option_length);
#endif
            connected = (option_result == 0) && (socket_error == 0);
        }
    }

    if (!set_nonblocking(socket, false))
    {
        connected = false;
    }
    return connected;
}

static socket_t connect_gateway(const sockaddr_in& address)
{
    socket_t socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket == BAD_SOCKET)
    {
        return BAD_SOCKET;
    }

    if (!connect_with_timeout(socket, address, CONNECT_ATTEMPT_TIMEOUT_MS) ||
        !set_receive_timeout(socket, PONG_TIMEOUT_MS))
    {
        close_sock(socket);
        return BAD_SOCKET;
    }

    return socket;
}

static bool send_all(socket_t socket, const uint8_t* data, size_t length)
{
    size_t sent_total = 0u;
    while (sent_total < length)
    {
#ifdef _WIN32
        const int sent = send(
            socket,
            reinterpret_cast<const char*>(data + sent_total),
            static_cast<int>(length - sent_total),
            0);
#else
        const ssize_t sent = send(
            socket, data + sent_total, length - sent_total, MSG_NOSIGNAL);
#endif
        if (sent <= 0)
        {
            return false;
        }
        sent_total += static_cast<size_t>(sent);
    }
    return true;
}

static int wait_until_readable(socket_t socket, uint32_t timeout_ms)
{
    fd_set read_set;
    FD_ZERO(&read_set);
    FD_SET(socket, &read_set);

    timeval timeout{};
    timeout.tv_sec = static_cast<long>(timeout_ms / 1000u);
    timeout.tv_usec = static_cast<long>((timeout_ms % 1000u) * 1000u);

#ifdef _WIN32
    return select(0, &read_set, nullptr, nullptr, &timeout);
#else
    return select(socket + 1, &read_set, nullptr, nullptr, &timeout);
#endif
}

static bool last_error_is_timeout()
{
#ifdef _WIN32
    const int error = WSAGetLastError();
    return (error == WSAETIMEDOUT) || (error == WSAEWOULDBLOCK);
#else
    return (errno == EAGAIN) || (errno == EWOULDBLOCK);
#endif
}

static WaitResult wait_for_pong(
    socket_t socket,
    uint16_t expected_seq,
    stream_parser_t& parser)
{
    uint8_t rx_buffer[512]{};
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(PONG_TIMEOUT_MS);

    while (!stop_requested())
    {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline)
        {
            return WaitResult::Timeout;
        }

        const auto remaining = std::chrono::duration_cast<
            std::chrono::milliseconds>(deadline - now).count();
        const uint32_t wait_ms = static_cast<uint32_t>(
            (remaining > 0) ? remaining : 1);
        const int ready = wait_until_readable(socket, wait_ms);
        if (ready == 0)
        {
            return WaitResult::Timeout;
        }
        if (ready < 0)
        {
            return stop_requested() ? WaitResult::Stopped : WaitResult::Error;
        }

#ifdef _WIN32
        const int received = recv(
            socket,
            reinterpret_cast<char*>(rx_buffer),
            static_cast<int>(sizeof(rx_buffer)),
            0);
#else
        const ssize_t received = recv(
            socket, rx_buffer, sizeof(rx_buffer), 0);
#endif
        if (received == 0)
        {
            return WaitResult::Error;
        }
        if (received < 0)
        {
            return last_error_is_timeout() ? WaitResult::Timeout :
                                             WaitResult::Error;
        }

        for (size_t index = 0u;
             index < static_cast<size_t>(received);
             index++)
        {
            protocol_packet_t packet{};
            const stream_parser_event_t event =
                stream_parser_feed_byte(&parser, rx_buffer[index], &packet);
            if ((event == STREAM_EVENT_FRAME) &&
                (packet.msg_id == MSG_PONG) &&
                (packet.seq == expected_seq) &&
                (packet.length == 0u))
            {
                return WaitResult::Pong;
            }
        }
    }

    return WaitResult::Stopped;
}

static bool send_ping(socket_t socket, uint16_t sequence)
{
    protocol_packet_t ping{};
    ping.version = PROTOCOL_VERSION;
    ping.msg_id = MSG_PING;
    ping.seq = sequence;

    uint8_t tx_buffer[PROTOCOL_MAX_FRAME_SIZE]{};
    size_t tx_length = 0u;
    if (protocol_encode(
          &ping,
          tx_buffer,
          sizeof(tx_buffer),
          &tx_length) != PROTO_OK)
    {
        return false;
    }

    return send_all(socket, tx_buffer, tx_length);
}

static void wait_until_or_stop(
    std::chrono::steady_clock::time_point deadline)
{
    while (!stop_requested())
    {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline)
        {
            return;
        }

        const auto remaining = deadline - now;
        const auto sleep_time = (remaining > std::chrono::milliseconds(10)) ?
            std::chrono::milliseconds(10) :
            std::chrono::duration_cast<std::chrono::milliseconds>(remaining);
        if (sleep_time.count() > 0)
        {
            std::this_thread::sleep_for(sleep_time);
        }
        else
        {
            std::this_thread::yield();
        }
    }
}

static uint16_t next_sequence(uint16_t sequence)
{
    return (sequence == UINT16_MAX) ? 1u :
                                      static_cast<uint16_t>(sequence + 1u);
}

int main()
{
    if (!net_init())
    {
        std::cerr << "Network initialization failed\n";
        return EXIT_FAILURE;
    }

    std::signal(SIGINT, handle_interrupt);

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(GATEWAY_PORT);
    if (inet_pton(AF_INET, GATEWAY_IP, &address.sin_addr) != 1)
    {
        std::cerr << "Invalid gateway IPv4 address\n";
        net_cleanup();
        return EXIT_FAILURE;
    }

    bool has_connected = false;
    uint32_t reconnect_count = 0u;
    uint16_t sequence = 1u;
    stream_parser_t parser{};
    stream_parser_init(&parser);

    std::cout << "TCP recovery client: "
              << GATEWAY_IP << ':' << GATEWAY_PORT
              << " (Ctrl+C to stop)\n";

    while (!stop_requested())
    {
        socket_t socket = connect_gateway(address);
        if (socket == BAD_SOCKET)
        {
            std::cout << "[CONNECT] failed / retry in "
                      << CONNECT_RETRY_MS << " ms\n";
            wait_until_or_stop(
                std::chrono::steady_clock::now() +
                std::chrono::milliseconds(CONNECT_RETRY_MS));
            continue;
        }

        stream_parser_init(&parser);
        if (!has_connected)
        {
            std::cout << "[CONNECT] success\n";
            has_connected = true;
        }
        else
        {
            reconnect_count++;
            std::cout << "[RECONNECT] success count="
                      << reconnect_count << '\n';
        }

        bool connected = true;
        auto next_ping_time = std::chrono::steady_clock::now();

        while (connected && !stop_requested())
        {
            wait_until_or_stop(next_ping_time);
            if (stop_requested())
            {
                break;
            }

            const auto ping_time = std::chrono::steady_clock::now();
            if (!send_ping(socket, sequence))
            {
                std::cout << "[DISCONNECT] send failed seq="
                          << sequence << '\n';
                connected = false;
                break;
            }

            const WaitResult wait_result =
                wait_for_pong(socket, sequence, parser);
            if (wait_result == WaitResult::Pong)
            {
                std::cout << "[PONG] seq=" << sequence << '\n';
                sequence = next_sequence(sequence);
                next_ping_time = ping_time +
                    std::chrono::milliseconds(PING_PERIOD_MS);
                continue;
            }

            if (wait_result == WaitResult::Timeout)
            {
                std::cout << "[DISCONNECT] PONG timeout seq="
                          << sequence << '\n';
            }
            else if (wait_result == WaitResult::Error)
            {
                std::cout << "[DISCONNECT] receive error seq="
                          << sequence << '\n';
            }
            connected = false;
        }

        close_sock(socket);
        stream_parser_init(&parser);
    }

    std::cout << "Stopped. reconnect_count=" << reconnect_count << '\n';
    net_cleanup();
    return EXIT_SUCCESS;
}
