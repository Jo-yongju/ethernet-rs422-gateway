#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#pragma comment(lib, "ws2_32.lib")

static uint16_t crc16_ccitt_false(const uint8_t* data, size_t len)
{
    uint16_t crc = 0xFFFF;

    for (size_t i = 0; i < len; ++i)
    {
        crc ^= (uint16_t)data[i] << 8;

        for (int bit = 0; bit < 8; ++bit)
        {
            if (crc & 0x8000)
                crc = (uint16_t)((crc << 1) ^ 0x1021);
            else
                crc <<= 1;
        }
    }

    return crc;
}

int main()
{
    const char* server_ip = "192.168.77.2";
    const uint16_t server_port = 5000;
    const uint16_t seq = 0x1234;

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        std::printf("WSAStartup failed\n");
        return 1;
    }

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET)
    {
        std::printf("socket() failed\n");
        WSACleanup();
        return 1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(server_port);

    if (inet_pton(AF_INET, server_ip, &addr.sin_addr) != 1)
    {
        std::printf("inet_pton() failed\n");
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR)
    {
        std::printf("connect() failed for %s:%u\n",
                    server_ip,
                    server_port);

        closesocket(sock);
        WSACleanup();
        return 1;
    }

    std::printf("Connected to %s:%u\n", server_ip, server_port);

    /*
     * Protocol V1
     *
     * MAGIC   : 0xAA55
     * VERSION : 0x01
     * MSG_ID  : PING = 0x01
     * SEQ     : 2 bytes, big endian
     * LENGTH  : 2 bytes, big endian
     * PAYLOAD : none
     * CRC16   : CRC16/CCITT-FALSE
     *
     * CRC covers VERSION .. PAYLOAD.
     */

    uint8_t frame[10];

    frame[0] = 0xAA;
    frame[1] = 0x55;

    frame[2] = 0x01;  // VERSION
    frame[3] = 0x01;  // PING

    frame[4] = (uint8_t)(seq >> 8);
    frame[5] = (uint8_t)(seq & 0xFF);

    frame[6] = 0x00;  // LENGTH MSB
    frame[7] = 0x00;  // LENGTH LSB

    // CRC excludes MAGIC and CRC itself.
    uint16_t correct_crc = crc16_ccitt_false(&frame[2], 6);

    // Deliberately corrupt one bit.
    uint16_t bad_crc = (uint16_t)(correct_crc ^ 0x0001);

    frame[8] = (uint8_t)(bad_crc >> 8);
    frame[9] = (uint8_t)(bad_crc & 0xFF);

    std::printf("SEQ         : %u\n", seq);
    std::printf("Correct CRC : 0x%04X\n", correct_crc);
    std::printf("Bad CRC     : 0x%04X\n", bad_crc);

    std::printf("TX frame    : ");
    for (size_t i = 0; i < sizeof(frame); ++i)
        std::printf("%02X ", frame[i]);
    std::printf("\n");

    int sent = send(
        sock,
        reinterpret_cast<const char*>(frame),
        sizeof(frame),
        0);

    if (sent != (int)sizeof(frame))
    {
        std::printf("send() failed or partial send: %d bytes\n", sent);

        closesocket(sock);
        WSACleanup();
        return 1;
    }

    std::printf("Sent intentionally corrupted CRC PING (%d bytes)\n", sent);
    std::printf("Expected: G474 parser discards this frame; no RS-422 forwarding/PONG.\n");

    closesocket(sock);
    WSACleanup();

    return 0;
}