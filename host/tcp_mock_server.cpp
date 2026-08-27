#include <cstdint>
#include <cstdlib>
#include <iostream>

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
#include "stream_parser.h"
}

static bool net_init() {
#ifdef _WIN32
    WSADATA wsa{};
    return WSAStartup(MAKEWORD(2,2), &wsa) == 0;
#else
    return true;
#endif
}

static void net_cleanup() {
#ifdef _WIN32
    WSACleanup();
#endif
}

static void close_sock(socket_t s) {
#ifdef _WIN32
    if (s != BAD_SOCKET) closesocket(s);
#else
    if (s != BAD_SOCKET) close(s);
#endif
}

static bool send_all(socket_t s, const uint8_t* data, size_t len) {
    size_t sent_total = 0;

    while (sent_total < len) {
#ifdef _WIN32
        int n = send(
            s,
            reinterpret_cast<const char*>(data + sent_total),
            static_cast<int>(len - sent_total),
            0);
#else
        ssize_t n = send(s, data + sent_total, len - sent_total, 0);
#endif
        if (n <= 0) return false;
        sent_total += static_cast<size_t>(n);
    }

    return true;
}

int main() {
    constexpr const char* IP = "127.0.0.1";
    constexpr uint16_t PORT = 5000;

    if (!net_init()) {
        std::cerr << "network init failed\n";
        return 1;
    }

    socket_t listen_s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_s == BAD_SOCKET) {
        std::cerr << "socket() failed\n";
        net_cleanup();
        return 1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, IP, &addr.sin_addr) != 1) {
        std::cerr << "inet_pton() failed\n";
        close_sock(listen_s);
        net_cleanup();
        return 1;
    }

    if (bind(listen_s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::cerr << "bind() failed (port 5000 may already be in use)\n";
        close_sock(listen_s);
        net_cleanup();
        return 1;
    }

    if (listen(listen_s, 1) != 0) {
        std::cerr << "listen() failed\n";
        close_sock(listen_s);
        net_cleanup();
        return 1;
    }

    std::cout << "TCP Mock Server\n";
    std::cout << "Listening on 127.0.0.1:5000...\n";

    socket_t client = accept(listen_s, nullptr, nullptr);
    if (client == BAD_SOCKET) {
        std::cerr << "accept() failed\n";
        close_sock(listen_s);
        net_cleanup();
        return 1;
    }

    std::cout << "Client connected\n";

    stream_parser_t parser{};
    stream_parser_init(&parser);
    uint8_t rxbuf[512]{};

    while (true) {
#ifdef _WIN32
        int n = recv(client, reinterpret_cast<char*>(rxbuf),
                     static_cast<int>(sizeof(rxbuf)), 0);
#else
        ssize_t n = recv(client, rxbuf, sizeof(rxbuf), 0);
#endif
        if (n == 0) {
            std::cout << "Client disconnected\n";
            break;
        }
        if (n < 0) {
            std::cerr << "recv() failed\n";
            break;
        }

        /* recv() may contain a partial frame or several frames. */
        for (size_t i = 0; i < static_cast<size_t>(n); ++i) {
            protocol_packet_t pkt{};
            stream_parser_event_t ev =
                stream_parser_feed_byte(&parser, rxbuf[i], &pkt);

            if (ev == STREAM_EVENT_FRAME && pkt.msg_id == MSG_PING) {
                std::cout << "RX PING seq=" << pkt.seq << "\n";

                protocol_packet_t pong{};
                pong.version = PROTOCOL_VERSION;
                pong.msg_id = MSG_PONG;
                pong.seq = pkt.seq;
                pong.length = 0;

                uint8_t txbuf[PROTOCOL_MAX_FRAME_SIZE]{};
                size_t txlen = 0;

                if (protocol_encode(&pong, txbuf, sizeof(txbuf), &txlen) != PROTO_OK) {
                    std::cerr << "PONG encode failed\n";
                    close_sock(client);
                    close_sock(listen_s);
                    net_cleanup();
                    return 1;
                }

                if (!send_all(client, txbuf, txlen)) {
                    std::cerr << "PONG send failed\n";
                    close_sock(client);
                    close_sock(listen_s);
                    net_cleanup();
                    return 1;
                }

                std::cout << "TX PONG seq=" << pong.seq << "\n";
            }
            else if (ev == STREAM_EVENT_ERROR) {
                std::cerr << "Parser rejected a candidate frame\n";
            }
        }
    }

    close_sock(client);
    close_sock(listen_s);
    net_cleanup();
    return 0;
}