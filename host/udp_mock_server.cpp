#include <chrono>
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
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
static constexpr socket_t BAD_SOCKET = -1;
#endif

extern "C" {
#include "protocol.h"
}

static bool net_init()
{
#ifdef _WIN32
    WSADATA wsa{};
    return WSAStartup(MAKEWORD(2,2), &wsa) == 0;
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

static void close_sock(socket_t s)
{
#ifdef _WIN32
    if (s != BAD_SOCKET) closesocket(s);
#else
    if (s != BAD_SOCKET) close(s);
#endif
}

int main()
{
    constexpr const char* DEST_IP = "127.0.0.1";
    constexpr uint16_t DEST_PORT = 5002;
    constexpr uint16_t TEST_COUNT = 100;
    constexpr uint16_t DROP_EVERY = 10;
    constexpr int PERIOD_MS = 10;

    if (!net_init()) {
        std::cerr << "network init failed\n";
        return 1;
    }

    socket_t s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == BAD_SOCKET) {
        std::cerr << "socket() failed\n";
        net_cleanup();
        return 1;
    }

    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(DEST_PORT);

    if (inet_pton(AF_INET, DEST_IP, &dest.sin_addr) != 1) {
        std::cerr << "inet_pton() failed\n";
        close_sock(s);
        net_cleanup();
        return 1;
    }

    std::cout << "UDP Mock Telemetry Server\n";
    std::cout << "Destination: " << DEST_IP << ":" << DEST_PORT << "\n";
    std::cout << "Planned telemetry: " << TEST_COUNT << "\n";
    std::cout << "Intentional drop: every " << DROP_EVERY << "th packet\n";

    uint32_t sent_count = 0;
    uint32_t dropped_count = 0;

    for (uint16_t seq = 1; seq <= TEST_COUNT; ++seq)
    {
        if ((seq % DROP_EVERY) == 0u)
        {
            ++dropped_count;
            std::cout << "DROP seq=" << seq << "\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(PERIOD_MS));
            continue;
        }

        protocol_packet_t telemetry{};
        telemetry.version = PROTOCOL_VERSION;
        telemetry.msg_id = MSG_TELEMETRY;
        telemetry.seq = seq;

        /*
         * Mock payload: 2-byte sample value, Big Endian.
         * sample_value = seq * 10
         */
        const uint16_t sample_value = static_cast<uint16_t>(seq * 10u);
        telemetry.length = 2u;
        telemetry.payload[0] = static_cast<uint8_t>((sample_value >> 8) & 0xFFu);
        telemetry.payload[1] = static_cast<uint8_t>(sample_value & 0xFFu);

        uint8_t frame[PROTOCOL_MAX_FRAME_SIZE]{};
        size_t frame_len = 0;

        if (protocol_encode(&telemetry, frame, sizeof(frame), &frame_len) != PROTO_OK)
        {
            std::cerr << "encode failed seq=" << seq << "\n";
            continue;
        }

#ifdef _WIN32
        const int n = sendto(
            s,
            reinterpret_cast<const char*>(frame),
            static_cast<int>(frame_len),
            0,
            reinterpret_cast<const sockaddr*>(&dest),
            sizeof(dest));
#else
        const ssize_t n = sendto(
            s,
            frame,
            frame_len,
            0,
            reinterpret_cast<const sockaddr*>(&dest),
            sizeof(dest));
#endif

        if (n <= 0)
        {
            std::cerr << "sendto() failed seq=" << seq << "\n";
            continue;
        }

        ++sent_count;
        std::cout << "TX TELEMETRY seq=" << seq << "\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(PERIOD_MS));
    }

    std::cout << "\n===== UDP SERVER SUMMARY =====\n";
    std::cout << "Planned : " << TEST_COUNT << "\n";
    std::cout << "Sent    : " << sent_count << "\n";
    std::cout << "Dropped : " << dropped_count << "\n";

    close_sock(s);
    net_cleanup();
    return 0;
}