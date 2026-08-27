#include <cstdint>
#include <cstdlib>
#include <iostream>
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
    std::string test;
    uint32_t timeout_ms = 1000;
    bool dry_run = false;
};

static void usage(const char* program)
{
    std::cout << "Usage: " << program
              << " --test bad-crc|unknown-msg|bad-length|seq-skip"
                 " [--ip ADDRESS] [--port PORT] [--timeout-ms MS] [--dry-run]\n";
}

static bool parse_u32(const char* text, uint32_t& value)
{
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(text, &end, 10);
    if ((text[0] == '\0') || (end == nullptr) || (*end != '\0')) return false;
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
            usage(argv[0]);
            std::exit(EXIT_SUCCESS);
        }
        if (arg == "--dry-run")
        {
            options.dry_run = true;
            continue;
        }
        if (i + 1 >= argc) return false;

        const char* value = argv[++i];
        uint32_t number = 0u;
        if (arg == "--ip") options.ip = value;
        else if (arg == "--test") options.test = value;
        else if (arg == "--port")
        {
            if (!parse_u32(value, number) || (number == 0u) || (number > 65535u))
                return false;
            options.port = static_cast<uint16_t>(number);
        }
        else if (arg == "--timeout-ms")
        {
            if (!parse_u32(value, number) || (number == 0u)) return false;
            options.timeout_ms = number;
        }
        else return false;
    }

    return (options.test == "bad-crc") ||
           (options.test == "unknown-msg") ||
           (options.test == "bad-length") ||
           (options.test == "seq-skip");
}

static std::vector<uint8_t> encode_packet(
    uint8_t msg_id,
    uint16_t seq,
    const std::vector<uint8_t>& payload)
{
    protocol_packet_t packet{};
    packet.version = PROTOCOL_VERSION;
    packet.msg_id = msg_id;
    packet.seq = seq;
    packet.length = static_cast<uint16_t>(payload.size());
    for (size_t i = 0u; i < payload.size(); ++i) packet.payload[i] = payload[i];

    std::vector<uint8_t> frame(PROTOCOL_MAX_FRAME_SIZE);
    size_t frame_length = 0u;
    if (protocol_encode(&packet, frame.data(), frame.size(), &frame_length) != PROTO_OK)
    {
        return {};
    }
    frame.resize(frame_length);
    return frame;
}

static std::vector<std::vector<uint8_t>> make_test_frames(const Options& options)
{
    std::vector<std::vector<uint8_t>> frames;
    if (options.test == "bad-crc")
    {
        frames.push_back(encode_packet(MSG_PING, 1u, {}));
        if (!frames[0].empty()) frames[0].back() ^= 0x01u;
    }
    else if (options.test == "unknown-msg")
    {
        frames.push_back(encode_packet(0x33u, 1u, {}));
    }
    else if (options.test == "bad-length")
    {
        frames.push_back(encode_packet(MSG_PING, 1u, {0x00u}));
    }
    else
    {
        frames.push_back(encode_packet(MSG_PING, 1u, {}));
        frames.push_back(encode_packet(MSG_PING, 3u, {}));
    }
    return frames;
}

static bool validate_generated_frames(
    const Options& options,
    const std::vector<std::vector<uint8_t>>& frames)
{
    if (frames.empty()) return false;
    protocol_packet_t packet{};
    const protocol_result_t first =
        protocol_decode(frames[0].data(), frames[0].size(), &packet);

    if (options.test == "bad-crc") return first == PROTO_ERR_CRC;
    if (first != PROTO_OK) return false;
    if (options.test == "unknown-msg") return packet.msg_id == 0x33u;
    if (options.test == "bad-length")
        return (packet.msg_id == MSG_PING) && (packet.length == 1u);

    protocol_packet_t second{};
    return (frames.size() == 2u) &&
           (packet.msg_id == MSG_PING) && (packet.seq == 1u) &&
           (protocol_decode(frames[1].data(), frames[1].size(), &second) == PROTO_OK) &&
           (second.msg_id == MSG_PING) && (second.seq == 3u);
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

static bool send_all(socket_t socket, const std::vector<uint8_t>& frame)
{
    size_t total = 0u;
    while (total < frame.size())
    {
#ifdef _WIN32
        const int sent = send(socket,
                              reinterpret_cast<const char*>(frame.data() + total),
                              static_cast<int>(frame.size() - total), 0);
#else
        const ssize_t sent = send(socket, frame.data() + total, frame.size() - total, 0);
#endif
        if (sent <= 0) return false;
        total += static_cast<size_t>(sent);
    }
    return true;
}

static bool receive_one(socket_t socket, protocol_packet_t& packet)
{
    stream_parser_t parser{};
    stream_parser_init(&parser);
    uint8_t buffer[512]{};
    while (true)
    {
#ifdef _WIN32
        const int received = recv(socket, reinterpret_cast<char*>(buffer),
                                  static_cast<int>(sizeof(buffer)), 0);
#else
        const ssize_t received = recv(socket, buffer, sizeof(buffer), 0);
#endif
        if (received <= 0) return false;
        for (size_t i = 0u; i < static_cast<size_t>(received); ++i)
        {
            if (stream_parser_feed_byte(&parser, buffer[i], &packet) == STREAM_EVENT_FRAME)
                return true;
        }
    }
}

int main(int argc, char** argv)
{
    Options options;
    if (!parse_options(argc, argv, options))
    {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    const auto frames = make_test_frames(options);
    if (!validate_generated_frames(options, frames))
    {
        std::cerr << "Fault frame generation self-check failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "Generated " << options.test << " frame set: PASS\n";
    if (options.dry_run) return EXIT_SUCCESS;

    if (!net_init()) return EXIT_FAILURE;
    socket_t socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket == BAD_SOCKET)
    {
        net_cleanup();
        return EXIT_FAILURE;
    }
    (void)set_receive_timeout(socket, options.timeout_ms);

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(options.port);
    if ((inet_pton(AF_INET, options.ip.c_str(), &address.sin_addr) != 1) ||
        (connect(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0))
    {
        std::cerr << "Connection failed\n";
        close_sock(socket);
        net_cleanup();
        return EXIT_FAILURE;
    }

    bool success = true;
    for (const auto& frame : frames)
    {
        if (!send_all(socket, frame))
        {
            success = false;
            break;
        }
        protocol_packet_t response{};
        const bool got_response = receive_one(socket, response);
        if (options.test == "bad-crc")
        {
            success = !got_response;
            std::cout << (success ? "No response to bad CRC: PASS\n"
                                  : "Unexpected response to bad CRC: FAIL\n");
        }
        else if (!got_response)
        {
            std::cout << "Response timeout: FAIL\n";
            success = false;
        }
        else
        {
            std::cout << "RX msg_id=0x" << std::hex
                      << static_cast<unsigned>(response.msg_id) << std::dec
                      << " seq=" << response.seq << '\n';
        }
    }

    close_sock(socket);
    net_cleanup();
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
