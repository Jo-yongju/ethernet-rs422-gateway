#include <stdio.h>
#include <string.h>

#include "crc16.h"
#include "protocol.h"

static int g_passed = 0;
static int g_failed = 0;

#define CHECK_TRUE(name, condition)                                      \
    do {                                                                 \
        if (condition) {                                                 \
            ++g_passed;                                                  \
            printf("[PASS] %s\n", name);                                 \
        } else {                                                         \
            ++g_failed;                                                  \
            printf("[FAIL] %s\n", name);                                 \
        }                                                                \
    } while (0)

static void test_crc_standard_vector(void)
{
    static const uint8_t input[] = "123456789";
    const uint16_t crc = crc16_ccitt_false(input, 9u);

    CHECK_TRUE(
        "CRC-16/CCITT-FALSE standard vector -> 0x29B1",
        crc == 0x29B1u);
}

static void test_ping_encode_decode(void)
{
    protocol_packet_t tx = {0};
    protocol_packet_t rx = {0};
    uint8_t frame[PROTOCOL_MAX_FRAME_SIZE] = {0};
    size_t frame_length = 0u;

    tx.version = PROTOCOL_VERSION;
    tx.msg_id = MSG_PING;
    tx.seq = 100u;
    tx.length = 0u;

    const protocol_result_t enc =
        protocol_encode(&tx, frame, sizeof(frame), &frame_length);

    const protocol_result_t dec =
        protocol_decode(frame, frame_length, &rx);

    CHECK_TRUE(
        "PING encode/decode round-trip",
        (enc == PROTO_OK) &&
        (dec == PROTO_OK) &&
        (frame_length == PROTOCOL_MIN_FRAME_SIZE) &&
        (rx.version == PROTOCOL_VERSION) &&
        (rx.msg_id == MSG_PING) &&
        (rx.seq == 100u) &&
        (rx.length == 0u));
}

static void test_payload_round_trip(void)
{
    protocol_packet_t tx = {0};
    protocol_packet_t rx = {0};
    uint8_t frame[PROTOCOL_MAX_FRAME_SIZE] = {0};
    size_t frame_length = 0u;

    /* Intentionally contains AA 55 inside payload. */
    static const uint8_t payload[] =
        {0x10u, 0x20u, 0xAAu, 0x55u, 0x30u};

    tx.version = PROTOCOL_VERSION;
    tx.msg_id = MSG_TELEMETRY;
    tx.seq = 42u;
    tx.length = (uint16_t)sizeof(payload);
    memcpy(tx.payload, payload, sizeof(payload));

    const protocol_result_t enc =
        protocol_encode(&tx, frame, sizeof(frame), &frame_length);

    const protocol_result_t dec =
        protocol_decode(frame, frame_length, &rx);

    CHECK_TRUE(
        "Payload round-trip including 0xAA55",
        (enc == PROTO_OK) &&
        (dec == PROTO_OK) &&
        (rx.msg_id == MSG_TELEMETRY) &&
        (rx.seq == 42u) &&
        (rx.length == sizeof(payload)) &&
        (memcmp(rx.payload, payload, sizeof(payload)) == 0));
}

static void test_crc_corruption_detection(void)
{
    protocol_packet_t tx = {0};
    protocol_packet_t rx = {0};
    uint8_t frame[PROTOCOL_MAX_FRAME_SIZE] = {0};
    size_t frame_length = 0u;

    tx.version = PROTOCOL_VERSION;
    tx.msg_id = MSG_PING;
    tx.seq = 7u;
    tx.length = 0u;

    const protocol_result_t enc =
        protocol_encode(&tx, frame, sizeof(frame), &frame_length);

    /* Corrupt MSG_ID while keeping the original CRC. */
    frame[3] ^= 0x01u;

    const protocol_result_t dec =
        protocol_decode(frame, frame_length, &rx);

    CHECK_TRUE(
        "CRC corruption detection",
        (enc == PROTO_OK) && (dec == PROTO_ERR_CRC));
}

static void test_invalid_magic(void)
{
    protocol_packet_t tx = {0};
    protocol_packet_t rx = {0};
    uint8_t frame[PROTOCOL_MAX_FRAME_SIZE] = {0};
    size_t frame_length = 0u;

    tx.version = PROTOCOL_VERSION;
    tx.msg_id = MSG_PING;
    tx.seq = 1u;
    tx.length = 0u;

    const protocol_result_t enc =
        protocol_encode(&tx, frame, sizeof(frame), &frame_length);

    frame[0] = 0xABu;

    const protocol_result_t dec =
        protocol_decode(frame, frame_length, &rx);

    CHECK_TRUE(
        "Invalid MAGIC detection",
        (enc == PROTO_OK) && (dec == PROTO_ERR_MAGIC));
}

static void test_invalid_version(void)
{
    protocol_packet_t tx = {0};
    protocol_packet_t rx = {0};
    uint8_t frame[PROTOCOL_MAX_FRAME_SIZE] = {0};
    size_t frame_length = 0u;

    tx.version = PROTOCOL_VERSION;
    tx.msg_id = MSG_PING;
    tx.seq = 1u;
    tx.length = 0u;

    const protocol_result_t enc =
        protocol_encode(&tx, frame, sizeof(frame), &frame_length);

    frame[2] = 0x02u;

    const protocol_result_t dec =
        protocol_decode(frame, frame_length, &rx);

    CHECK_TRUE(
        "Unsupported VERSION detection",
        (enc == PROTO_OK) && (dec == PROTO_ERR_VERSION));
}

static void test_oversized_length(void)
{
    protocol_packet_t rx = {0};
    uint8_t frame[PROTOCOL_MIN_FRAME_SIZE] = {0};

    /*
     * Minimal artificial frame:
     * MAGIC = AA55
     * VERSION = 01
     * MSG_ID = 01
     * SEQ = 0001
     * LENGTH = 0101 = 257 (> 256)
     *
     * Decoder must reject based on LENGTH before trying to use payload.
     */
    frame[0] = 0xAAu;
    frame[1] = 0x55u;
    frame[2] = PROTOCOL_VERSION;
    frame[3] = MSG_PING;
    frame[4] = 0x00u;
    frame[5] = 0x01u;
    frame[6] = 0x01u;
    frame[7] = 0x01u;

    const protocol_result_t dec =
        protocol_decode(frame, sizeof(frame), &rx);

    CHECK_TRUE(
        "Oversized LENGTH detection",
        dec == PROTO_ERR_LENGTH);
}

static void test_frame_size_mismatch(void)
{
    protocol_packet_t tx = {0};
    protocol_packet_t rx = {0};
    uint8_t frame[PROTOCOL_MAX_FRAME_SIZE] = {0};
    size_t frame_length = 0u;

    tx.version = PROTOCOL_VERSION;
    tx.msg_id = MSG_PING;
    tx.seq = 5u;
    tx.length = 0u;

    const protocol_result_t enc =
        protocol_encode(&tx, frame, sizeof(frame), &frame_length);

    const protocol_result_t dec =
        protocol_decode(frame, frame_length - 1u, &rx);

    CHECK_TRUE(
        "Truncated frame detection",
        (enc == PROTO_OK) &&
        ((dec == PROTO_ERR_BUFFER) || (dec == PROTO_ERR_FRAME_SIZE)));
}

static void test_small_output_buffer(void)
{
    protocol_packet_t tx = {0};
    uint8_t tiny_buffer[4] = {0};
    size_t frame_length = 0u;

    tx.version = PROTOCOL_VERSION;
    tx.msg_id = MSG_PING;
    tx.seq = 9u;
    tx.length = 0u;

    const protocol_result_t enc =
        protocol_encode(&tx, tiny_buffer, sizeof(tiny_buffer), &frame_length);

    CHECK_TRUE(
        "Encode output buffer bounds check",
        enc == PROTO_ERR_BUFFER);
}

int main(void)
{
    printf("Protocol V1 Unit Test\n");
    printf("=====================\n");

    test_crc_standard_vector();
    test_ping_encode_decode();
    test_payload_round_trip();
    test_crc_corruption_detection();
    test_invalid_magic();
    test_invalid_version();
    test_oversized_length();
    test_frame_size_mismatch();
    test_small_output_buffer();

    printf("\nResult: %d passed, %d failed\n", g_passed, g_failed);

    return (g_failed == 0) ? 0 : 1;
}