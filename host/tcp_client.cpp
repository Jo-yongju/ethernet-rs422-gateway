#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
static constexpr socket_t BAD_SOCKET = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <cerrno>
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

struct Options
{
    std::string ip = "127.0.0.1";
    uint16_t port = 5000;
    uint32_t count = 100;
    uint32_t timeout_ms = 1000;
    std::string csv_file = "tcp_rtt.csv";
};

enum class WaitResult { Pong, Timeout, Error };

static void print_usage(const char* program)
{
    std::cout << "Usage: " << program
              << " [--ip ADDRESS] [--port PORT] [--count N]"
                 " [--timeout-ms MS] [--csv FILE]\n";
}

static bool parse_u32(const char* text, uint32_t& value)
{
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(text, &end, 10);
    if ((text[0] == '\0') || (end == nullptr) || (*end != '\0'))
    {
        return false;
    }
    value = static_cast<uint32_t>(parsed);
    return static_cast<unsigned long>(value) == parsed;
}

static bool parse_options(int argc, char** argv, Options& options)
{
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if ((arg == "--help") || (arg == "-h"))
        {
            print_usage(argv[0]);
            std::exit(EXIT_SUCCESS);
        }
        if (i + 1 >= argc)
        {
            std::cerr << "Missing value for " << arg << '\n';
            return false;
        }

        const char* value = argv[++i];
        uint32_t number = 0;
        if (arg == "--ip")
        {
            options.ip = value;
        }
        else if (arg == "--port")
        {
            if (!parse_u32(value, number) || (number == 0u) || (number > 65535u))
            {
                std::cerr << "Invalid port\n";
                return false;
            }
            options.port = static_cast<uint16_t>(number);
        }
        else if (arg == "--count")
        {
            if (!parse_u32(value, number) || (number == 0u) || (number > 1000000u))
            {
                std::cerr << "Invalid count (1..1000000)\n";
                return false;
            }
            options.count = number;
        }
        else if (arg == "--timeout-ms")
        {
            if (!parse_u32(value, number) || (number == 0u) || (number > 600000u))
            {
                std::cerr << "Invalid timeout\n";
                return false;
            }
            options.timeout_ms = number;
        }
        else if (arg == "--csv")
        {
            options.csv_file = value;
        }
        else
        {
            std::cerr << "Unknown option: " << arg << '\n';
            return false;
        }
    }
    return true;
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
    if (socket != BAD_SOCKET) closesocket(socket);
#else
    if (socket != BAD_SOCKET) close(socket);
#endif
}

static bool set_receive_timeout(socket_t socket, uint32_t timeout_ms)
{
#ifdef _WIN32
    const DWORD timeout = static_cast<DWORD>(timeout_ms);
    return setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
                      reinterpret_cast<const char*>(&timeout), sizeof(timeout)) == 0;
#else
    timeval timeout{};
    timeout.tv_sec = static_cast<time_t>(timeout_ms / 1000u);
    timeout.tv_usec = static_cast<suseconds_t>((timeout_ms % 1000u) * 1000u);
    return setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
                      &timeout, sizeof(timeout)) == 0;
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

static bool send_all(socket_t socket, const uint8_t* data, size_t length)
{
    size_t sent_total = 0u;
    while (sent_total < length)
    {
#ifdef _WIN32
        const int sent = send(socket,
                              reinterpret_cast<const char*>(data + sent_total),
                              static_cast<int>(length - sent_total), 0);
#else
        const ssize_t sent = send(socket, data + sent_total, length - sent_total, 0);
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

static WaitResult wait_for_pong(
    socket_t socket,
    uint16_t expected_seq,
    stream_parser_t& parser,
    uint32_t timeout_ms)
{
    uint8_t rx_buffer[512]{};
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(timeout_ms);

    while (true)
    {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline)
        {
            return WaitResult::Timeout;
        }
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - now).count();
        const uint32_t wait_ms = static_cast<uint32_t>(
            (remaining > 0) ? remaining : 1);
        const int ready = wait_until_readable(socket, wait_ms);
        if (ready == 0)
        {
            return WaitResult::Timeout;
        }
        if (ready < 0)
        {
            return WaitResult::Error;
        }

#ifdef _WIN32
        const int received = recv(socket,
                                  reinterpret_cast<char*>(rx_buffer),
                                  static_cast<int>(sizeof(rx_buffer)), 0);
#else
        const ssize_t received = recv(socket, rx_buffer, sizeof(rx_buffer), 0);
#endif
        if (received == 0)
        {
            return WaitResult::Error;
        }
        if (received < 0)
        {
            return last_error_is_timeout() ? WaitResult::Timeout : WaitResult::Error;
        }

        for (size_t i = 0u; i < static_cast<size_t>(received); ++i)
        {
            protocol_packet_t packet{};
            const stream_parser_event_t event =
                stream_parser_feed_byte(&parser, rx_buffer[i], &packet);
            if ((event == STREAM_EVENT_FRAME) &&
                (packet.msg_id == MSG_PONG) &&
                (packet.seq == expected_seq) &&
                (packet.length == 0u))
            {
                return WaitResult::Pong;
            }
        }
    }
}

int main(int argc, char** argv)
{
    Options options;
    if (!parse_options(argc, argv, options))
    {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
    if (!net_init())
    {
        std::cerr << "Network initialization failed\n";
        return EXIT_FAILURE;
    }

    socket_t socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket == BAD_SOCKET)
    {
        std::cerr << "socket() failed\n";
        net_cleanup();
        return EXIT_FAILURE;
    }
    if (!set_receive_timeout(socket, options.timeout_ms))
    {
        std::cerr << "Failed to configure receive timeout\n";
        close_sock(socket);
        net_cleanup();
        return EXIT_FAILURE;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(options.port);
    if (inet_pton(AF_INET, options.ip.c_str(), &address.sin_addr) != 1)
    {
        std::cerr << "Invalid IPv4 address: " << options.ip << '\n';
        close_sock(socket);
        net_cleanup();
        return EXIT_FAILURE;
    }
    if (connect(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0)
    {
        std::cerr << "connect() failed for " << options.ip << ':' << options.port << '\n';
        close_sock(socket);
        net_cleanup();
        return EXIT_FAILURE;
    }

    std::ofstream csv(options.csv_file);
    if (!csv.is_open())
    {
        std::cerr << "Failed to open " << options.csv_file << '\n';
        close_sock(socket);
        net_cleanup();
        return EXIT_FAILURE;
    }
    csv << "seq,rtt_ms,status\n";
    std::cout << "Connected to " << options.ip << ':' << options.port << '\n';

    stream_parser_t parser{};
    stream_parser_init(&parser);
    std::vector<double> rtts;
    rtts.reserve(options.count);
    uint32_t tx_count = 0u;
    uint32_t rx_count = 0u;
    uint32_t timeout_count = 0u;
    bool fatal_error = false;

    for (uint32_t request = 0u; request < options.count; ++request)
    {
        const uint16_t seq = static_cast<uint16_t>(request + 1u);
        protocol_packet_t ping{};
        ping.version = PROTOCOL_VERSION;
        ping.msg_id = MSG_PING;
        ping.seq = seq;

        uint8_t tx_buffer[PROTOCOL_MAX_FRAME_SIZE]{};
        size_t tx_length = 0u;
        if (protocol_encode(&ping, tx_buffer, sizeof(tx_buffer), &tx_length) != PROTO_OK)
        {
            csv << seq << ",,ENCODE_ERROR\n";
            fatal_error = true;
            break;
        }

        const auto tx_time = std::chrono::steady_clock::now();
        if (!send_all(socket, tx_buffer, tx_length))
        {
            csv << seq << ",,SEND_ERROR\n";
            fatal_error = true;
            break;
        }
        ++tx_count;

        const WaitResult result =
            wait_for_pong(socket, seq, parser, options.timeout_ms);
        if (result == WaitResult::Timeout)
        {
            ++timeout_count;
            csv << seq << ",,TIMEOUT\n";
            std::cout << "SEQ=" << seq << " TIMEOUT\n";
            stream_parser_reset_frame(&parser);
            continue;
        }
        if (result == WaitResult::Error)
        {
            csv << seq << ",,RX_ERROR\n";
            fatal_error = true;
            break;
        }

        const double rtt_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - tx_time).count();
        ++rx_count;
        rtts.push_back(rtt_ms);
        csv << seq << ',' << std::fixed << std::setprecision(6)
            << rtt_ms << ",OK\n";
        std::cout << "SEQ=" << std::setw(5) << seq << " RTT="
                  << std::fixed << std::setprecision(3) << rtt_ms << " ms\n";
    }

    std::cout << "\n===== TCP RTT TEST =====\n"
              << "TX        : " << tx_count << '\n'
              << "RX        : " << rx_count << '\n'
              << "Timeout   : " << timeout_count << '\n';

    if (!rtts.empty())
    {
        const auto extrema = std::minmax_element(rtts.begin(), rtts.end());
        const double average =
            std::accumulate(rtts.begin(), rtts.end(), 0.0) / rtts.size();
        double jitter_sum = 0.0;
        for (size_t i = 1u; i < rtts.size(); ++i)
        {
            jitter_sum += std::abs(rtts[i] - rtts[i - 1u]);
        }
        const double jitter = (rtts.size() > 1u)
            ? jitter_sum / static_cast<double>(rtts.size() - 1u)
            : 0.0;
        std::cout << std::fixed << std::setprecision(3)
                  << "RTT Min   : " << *extrema.first << " ms\n"
                  << "RTT Avg   : " << average << " ms\n"
                  << "RTT Max   : " << *extrema.second << " ms\n"
                  << "Jitter    : " << jitter << " ms\n";
    }
    else
    {
        std::cout << "RTT Min/Avg/Max/Jitter: N/A\n";
    }
    std::cout << "CSV saved : " << options.csv_file << '\n';

    close_sock(socket);
    net_cleanup();
    return (!fatal_error && (rx_count == tx_count)) ? EXIT_SUCCESS : EXIT_FAILURE;
}
