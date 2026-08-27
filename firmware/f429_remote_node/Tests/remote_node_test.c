#include <stdio.h>
#include <string.h>

#include "protocol.h"
#include "remote_node.h"
#include "uart_rx_ring.h"

#define CAPTURED_PACKET_COUNT 32u

typedef struct
{
    uint32_t now_ms;
    protocol_packet_t packets[CAPTURED_PACKET_COUNT];
    size_t packet_count;
} test_context_t;

static int passed;
static int failed;

#define CHECK(name, condition)                                            \
    do                                                                   \
    {                                                                    \
        if (condition)                                                    \
        {                                                                \
            ++passed;                                                     \
            printf("[PASS] %s\n", name);                                \
        }                                                                \
        else                                                             \
        {                                                                \
            ++failed;                                                     \
            printf("[FAIL] %s\n", name);                                \
        }                                                                \
    } while (0)

static uint32_t test_time(void *user_context)
{
    return ((test_context_t *)user_context)->now_ms;
}

static int capture_tx(
    const uint8_t *frame,
    size_t frame_length,
    void *user_context)
{
    test_context_t *context = (test_context_t *)user_context;

    if (context->packet_count >= CAPTURED_PACKET_COUNT)
    {
        return -1;
    }

    if (protocol_decode(
            frame,
            frame_length,
            &context->packets[context->packet_count]) != PROTO_OK)
    {
        return -1;
    }

    context->packet_count++;
    return 0;
}

static uint16_t read_u16_be(const uint8_t *src)
{
    return (uint16_t)(((uint16_t)src[0] << 8) | src[1]);
}

static uint32_t read_u32_be(const uint8_t *src)
{
    return ((uint32_t)src[0] << 24) |
           ((uint32_t)src[1] << 16) |
           ((uint32_t)src[2] << 8) |
           (uint32_t)src[3];
}

static void feed_packet(
    remote_node_t *node,
    uint8_t msg_id,
    uint16_t seq,
    const uint8_t *payload,
    uint16_t payload_length)
{
    protocol_packet_t packet;
    uint8_t frame[PROTOCOL_MAX_FRAME_SIZE];
    size_t frame_length = 0u;

    memset(&packet, 0, sizeof(packet));
    packet.version = PROTOCOL_VERSION;
    packet.msg_id = msg_id;
    packet.seq = seq;
    packet.length = payload_length;
    if (payload_length != 0u)
    {
        memcpy(packet.payload, payload, payload_length);
    }

    if (protocol_encode(&packet, frame, sizeof(frame), &frame_length) == PROTO_OK)
    {
        remote_node_process_bytes(node, frame, frame_length);
    }
}

static void test_ping_and_duplicate(void)
{
    test_context_t context;
    remote_node_t node;

    memset(&context, 0, sizeof(context));
    remote_node_init(&node, test_time, capture_tx, &context);

    feed_packet(&node, MSG_PING, 152u, NULL, 0u);
    feed_packet(&node, MSG_PING, 152u, NULL, 0u);

    CHECK(
        "PING produces PONG with echoed sequence",
        (context.packet_count == 2u) &&
        (context.packets[0].msg_id == MSG_PONG) &&
        (context.packets[0].seq == 152u) &&
        (context.packets[0].length == 0u));
    CHECK(
        "Duplicate request is counted and answered again",
        (node.stats.duplicate_seq_count == 1u) &&
        (context.packets[1].msg_id == MSG_PONG) &&
        (context.packets[1].seq == 152u));
    CHECK(
        "Valid request statistics and communication state",
        (node.stats.valid_frame_count == 2u) &&
        (node.stats.last_rx_seq == 152u) &&
        (node.stats.comm_status == COMM_OK));
}

static void test_application_errors_and_crc(void)
{
    const uint8_t bad_payload = 0x5Au;
    test_context_t context;
    remote_node_t node;
    protocol_packet_t corrupt_packet;
    uint8_t frame[PROTOCOL_MAX_FRAME_SIZE];
    size_t frame_length = 0u;

    memset(&context, 0, sizeof(context));
    remote_node_init(&node, test_time, capture_tx, &context);

    feed_packet(&node, MSG_PING, 1u, &bad_payload, 1u);
    feed_packet(&node, 0x33u, 2u, NULL, 0u);

    memset(&corrupt_packet, 0, sizeof(corrupt_packet));
    corrupt_packet.version = PROTOCOL_VERSION;
    corrupt_packet.msg_id = MSG_PING;
    corrupt_packet.seq = 3u;
    (void)protocol_encode(
        &corrupt_packet,
        frame,
        sizeof(frame),
        &frame_length);
    frame[frame_length - 1u] ^= 0x01u;
    remote_node_process_bytes(&node, frame, frame_length);

    CHECK(
        "Application BAD_LENGTH returns ERROR",
        (context.packet_count >= 1u) &&
        (context.packets[0].msg_id == MSG_ERROR) &&
        (context.packets[0].seq == 1u) &&
        (context.packets[0].length == 2u) &&
        (context.packets[0].payload[0] == REMOTE_ERROR_BAD_LENGTH) &&
        (context.packets[0].payload[1] == MSG_PING));
    CHECK(
        "Unknown message returns ERROR and increments counter",
        (context.packet_count == 2u) &&
        (context.packets[1].msg_id == MSG_ERROR) &&
        (context.packets[1].payload[0] == REMOTE_ERROR_UNKNOWN_MSG) &&
        (context.packets[1].payload[1] == 0x33u) &&
        (node.stats.unknown_msg_count == 1u));
    CHECK(
        "CRC-corrupt frame is dropped without ERROR response",
        (context.packet_count == 2u) &&
        (node.stats.crc_error_count == 1u) &&
        (node.stats.valid_frame_count == 2u));
}

static void test_status_response(void)
{
    test_context_t context;
    remote_node_t node;
    const protocol_packet_t *status;
    protocol_packet_t corrupt_packet;
    uint8_t corrupt_frame[PROTOCOL_MAX_FRAME_SIZE];
    size_t corrupt_length = 0u;
    const uint8_t oversized_header[PROTOCOL_FIXED_HEADER_SIZE] =
    {
        0xAAu, 0x55u, PROTOCOL_VERSION, MSG_PING,
        0x00u, 0x01u, 0x01u, 0x01u
    };

    memset(&context, 0, sizeof(context));
    context.now_ms = 0x01020304u;
    remote_node_init(&node, test_time, capture_tx, &context);

    memset(&corrupt_packet, 0, sizeof(corrupt_packet));
    corrupt_packet.version = PROTOCOL_VERSION;
    corrupt_packet.msg_id = MSG_PING;
    corrupt_packet.seq = 0x0010u;
    (void)protocol_encode(
        &corrupt_packet,
        corrupt_frame,
        sizeof(corrupt_frame),
        &corrupt_length);
    corrupt_frame[corrupt_length - 1u] ^= 0x01u;
    remote_node_process_bytes(&node, corrupt_frame, corrupt_length);
    remote_node_process_bytes(
        &node,
        oversized_header,
        sizeof(oversized_header));
    feed_packet(&node, MSG_READ_STATUS, 0x1234u, NULL, 0u);

    status = &context.packets[0];
    CHECK(
        "READ_STATUS produces 20-byte STATUS_RESP",
        (context.packet_count == 1u) &&
        (status->msg_id == MSG_STATUS_RESP) &&
        (status->seq == 0x1234u) &&
        (status->length == 20u));
    CHECK(
        "STATUS_RESP fields are explicit Big Endian values",
        (read_u32_be(&status->payload[0]) == 0x01020304u) &&
        (read_u32_be(&status->payload[4]) == 1u) &&
        (read_u32_be(&status->payload[8]) == 1u) &&
        (read_u32_be(&status->payload[12]) == 1u) &&
        (read_u16_be(&status->payload[16]) == 0x1234u) &&
        (status->payload[18] == COMM_OK) &&
        (status->payload[19] == 0u));
}

static void test_periodic_frames_and_wrap(void)
{
    test_context_t context;
    remote_node_t node;

    memset(&context, 0, sizeof(context));
    remote_node_init(&node, test_time, capture_tx, &context);
    remote_node_set_status_flags(&node, 0x1234u);

    context.now_ms = 100u;
    remote_node_process_periodic(&node);
    context.now_ms = 500u;
    remote_node_process_periodic(&node);

    CHECK(
        "TELEMETRY is 10 Hz with uptime and status flags",
        (context.packet_count >= 1u) &&
        (context.packets[0].msg_id == MSG_TELEMETRY) &&
        (context.packets[0].seq == 1u) &&
        (context.packets[0].length == 6u) &&
        (read_u32_be(&context.packets[0].payload[0]) == 100u) &&
        (read_u16_be(&context.packets[0].payload[4]) == 0x1234u));
    CHECK(
        "HEARTBEAT uses an independent sequence space",
        (context.packet_count == 3u) &&
        (context.packets[1].msg_id == MSG_TELEMETRY) &&
        (context.packets[1].seq == 2u) &&
        (context.packets[2].msg_id == MSG_HEARTBEAT) &&
        (context.packets[2].seq == 1u) &&
        (context.packets[2].length == 4u));

    node.telemetry_seq = 0xFFFFu;
    node.heartbeat_seq = 0xFFFFu;
    context.now_ms = 1000u;
    remote_node_process_periodic(&node);
    CHECK(
        "Periodic sequence counters wrap independently to zero",
        (context.packets[3].msg_id == MSG_TELEMETRY) &&
        (context.packets[3].seq == 0u) &&
        (context.packets[4].msg_id == MSG_HEARTBEAT) &&
        (context.packets[4].seq == 0u));
}

static void test_ring_bounds(void)
{
    uart_rx_ring_t ring;
    uint8_t byte = 0u;
    uint32_t pushed = 0u;
    uint32_t popped = 0u;

    uart_rx_ring_init(&ring);
    while (uart_rx_ring_push_isr(&ring, (uint8_t)pushed) != 0)
    {
        pushed++;
    }
    while (uart_rx_ring_pop(&ring, &byte) != 0)
    {
        popped++;
    }

    CHECK(
        "RX ring rejects full writes without crossing bounds",
        (pushed == (UART_RX_RING_SIZE - 1u)) &&
        (popped == pushed));
    CHECK(
        "RX ring is reusable after index wrap",
        (uart_rx_ring_push_isr(&ring, 0xA5u) != 0) &&
        (uart_rx_ring_pop(&ring, &byte) != 0) &&
        (byte == 0xA5u));
}

int main(void)
{
    printf("F429 Remote Node Host Unit Test\n");
    printf("===============================\n");

    test_ping_and_duplicate();
    test_application_errors_and_crc();
    test_status_response();
    test_periodic_frames_and_wrap();
    test_ring_bounds();

    printf("\nResult: %d passed, %d failed\n", passed, failed);
    return (failed == 0) ? 0 : 1;
}
