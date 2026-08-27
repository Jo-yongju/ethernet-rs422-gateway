#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
static constexpr socket_t BAD_SOCKET = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
using socket_t = int;
static constexpr socket_t BAD_SOCKET = -1;
#endif

extern "C" {
#include "protocol.h"
}

struct Options
{
    std::string bind_ip = "127.0.0.1";
    uint16_t port = 5002;
    uint32_t expected = 100;
    uint32_t initial_timeout_ms = 10000;
    uint32_t idle_timeout_ms = 1500;
    std::string csv_file = "udp_loss.csv";
};

static void print_usage(const char* program)
{
    std::cout << "Usage: " << program
              << " [--bind-ip ADDRESS] [--port PORT] [--expected N]"
                 " [--initial-timeout-ms MS] [--idle-timeout-ms MS]"
                 " [--csv FILE]\n";
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
            return false;
        }

        const char* value = argv[++i];
        uint32_t number = 0u;
        if (arg == "--bind-ip")
        {
            options.bind_ip = value;
        }
        else if (arg == "--port")
        {
            if (!parse_u32(value, number) || (number == 0u) || (number > 65535u))
            {
                return false;
            }
            options.port = static_cast<uint16_t>(number);
        }
        else if (arg == "--expected")
        {
            if (!parse_u32(value, number) || (number == 0u) || (number > 65536u))
            {
                return false;
            }
            options.expected = number;
        }
        else if (arg == "--initial-timeout-ms")
        {
            if (!parse_u32(value, number) || (number == 0u))
            {
                return false;
            }
            options.initial_timeout_ms = number;
        }
        else if (arg == "--idle-timeout-ms")
        {
            if (!parse_u32(value, number) || (number == 0u))
            {
                return false;
            }
            options.idle_timeout_ms = number;
        }
        else if (arg == "--csv")
        {
            options.csv_file = value;
        }
        else
        {
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

static uint16_t read_u16_be(const uint8_t* source)
{
    return static_cast<uint16_t>(
        (static_cast<uint16_t>(source[0]) << 8) | source[1]);
}

static uint32_t read_u32_be(const uint8_t* source)
{
    return (static_cast<uint32_t>(source[0]) << 24) |
           (static_cast<uint32_t>(source[1]) << 16) |
           (static_cast<uint32_t>(source[2]) << 8) |
           static_cast<uint32_t>(source[3]);
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

    socket_t socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket == BAD_SOCKET)
    {
        std::cerr << "socket() failed\n";
        net_cleanup();
        return EXIT_FAILURE;
    }

    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_port = htons(options.port);
    if (inet_pton(AF_INET, options.bind_ip.c_str(), &local.sin_addr) != 1)
    {
        std::cerr << "Invalid bind IPv4 address\n";
        close_sock(socket);
        net_cleanup();
        return EXIT_FAILURE;
    }
    if (bind(socket, reinterpret_cast<sockaddr*>(&local), sizeof(local)) != 0)
    {
        std::cerr << "bind() failed for " << options.bind_ip << ':' << options.port << '\n';
        close_sock(socket);
        net_cleanup();
        return EXIT_FAILURE;
    }
    if (!set_receive_timeout(socket, options.initial_timeout_ms))
    {
        std::cerr << "Failed to configure initial receive timeout\n";
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
    csv << "seq,uptime_ms,status_flags,legacy_sample,status\n";

    std::cout << "UDP Telemetry Client\nListening on "
              << options.bind_ip << ':' << options.port << "...\n";

    std::set<uint16_t> received_sequences;
    uint32_t crc_errors = 0u;
    uint32_t invalid_frames = 0u;
    uint32_t duplicate_frames = 0u;
    bool first_packet_received = false;
    uint16_t first_seq = 0u;

    while (received_sequences.size() < options.expected)
    {
        uint8_t rx_buffer[PROTOCOL_MAX_FRAME_SIZE]{};
#ifdef _WIN32
        const int received = recvfrom(socket,
                                      reinterpret_cast<char*>(rx_buffer),
                                      static_cast<int>(sizeof(rx_buffer)),
                                      0, nullptr, nullptr);
#else
        const ssize_t received = recvfrom(socket, rx_buffer, sizeof(rx_buffer),
                                          0, nullptr, nullptr);
#endif
        if (received <= 0)
        {
            std::cout << (first_packet_received
                ? "Idle timeout -> test finished\n"
                : "Initial wait timeout -> no telemetry received\n");
            break;
        }

        protocol_packet_t packet{};
        const protocol_result_t result =
            protocol_decode(rx_buffer, static_cast<size_t>(received), &packet);
        if (result != PROTO_OK)
        {
            if (result == PROTO_ERR_CRC) ++crc_errors;
            else ++invalid_frames;
            continue;
        }
        if ((packet.msg_id != MSG_TELEMETRY) ||
            ((packet.length != 6u) && (packet.length != 2u)))
        {
            ++invalid_frames;
            continue;
        }

        if (!first_packet_received)
        {
            first_packet_received = true;
            first_seq = packet.seq;
            if (!set_receive_timeout(socket, options.idle_timeout_ms))
            {
                std::cerr << "Failed to configure idle timeout\n";
                break;
            }
        }

        const bool inserted = received_sequences.insert(packet.seq).second;
        if (!inserted)
        {
            ++duplicate_frames;
        }

        if (packet.length == 6u)
        {
            const uint32_t uptime_ms = read_u32_be(&packet.payload[0]);
            const uint16_t flags = read_u16_be(&packet.payload[4]);
            csv << packet.seq << ',' << uptime_ms << ',' << flags << ",,"
                << (inserted ? "OK" : "DUPLICATE") << '\n';
            std::cout << "RX TELEMETRY seq=" << packet.seq
                      << " uptime=" << uptime_ms
                      << " flags=0x" << std::hex << flags << std::dec << '\n';
        }
        else
        {
            const uint16_t sample = read_u16_be(packet.payload);
            csv << packet.seq << ",,," << sample << ','
                << (inserted ? "OK_LEGACY" : "DUPLICATE") << '\n';
            std::cout << "RX TELEMETRY seq=" << packet.seq
                      << " legacy_sample=" << sample << '\n';
        }
    }

    uint32_t missing_count = 0u;
    if (first_packet_received)
    {
        for (uint32_t offset = 0u; offset < options.expected; ++offset)
        {
            const uint16_t seq = static_cast<uint16_t>(first_seq + offset);
            if (received_sequences.find(seq) == received_sequences.end())
            {
                ++missing_count;
                csv << seq << ",,,,MISSING\n";
            }
        }
    }
    else
    {
        missing_count = options.expected;
    }

    const uint32_t unique_received =
        static_cast<uint32_t>(received_sequences.size());
    const double loss_percent = 100.0 * static_cast<double>(missing_count) /
                                static_cast<double>(options.expected);
    std::cout << "\n===== UDP LOSS TEST =====\n"
              << "Expected   : " << options.expected << '\n'
              << "Received   : " << unique_received << '\n'
              << "Missing    : " << missing_count << '\n'
              << "Duplicates : " << duplicate_frames << '\n'
              << std::fixed << std::setprecision(2)
              << "Packet Loss: " << loss_percent << " %\n"
              << "CRC Errors : " << crc_errors << '\n'
              << "Invalid    : " << invalid_frames << '\n'
              << "CSV saved  : " << options.csv_file << '\n';

    close_sock(socket);
    net_cleanup();
    return first_packet_received ? EXIT_SUCCESS : EXIT_FAILURE;
}
