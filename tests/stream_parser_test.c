#include <stdio.h>
#include <string.h>

#include "protocol.h"
#include "stream_parser.h"

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

static size_t make_frame(
    uint8_t msg_id,
    uint16_t seq,
    const uint8_t *payload,
    uint16_t payload_length,
    uint8_t *out,
    size_t out_size)
{
    protocol_packet_t packet = {0};
    size_t frame_length = 0u;

    packet.version = PROTOCOL_VERSION;
    packet.msg_id = msg_id;
    packet.seq = seq;
    packet.length = payload_length;

    if ((payload_length > 0u) && (payload != NULL))
    {
        memcpy(packet.payload, payload, payload_length);
    }

    if (protocol_encode(&packet, out, out_size, &frame_length) != PROTO_OK)
    {
        return 0u;
    }

    return frame_length;
}

static int feed_and_count_frames(
    stream_parser_t *parser,
    const uint8_t *data,
    size_t length,
    protocol_packet_t *last_packet,
    int *error_count)
{
    int frames = 0;

    for (size_t i = 0u; i < length; ++i)
    {
        protocol_packet_t packet = {0};
        const stream_parser_event_t ev =
            stream_parser_feed_byte(parser, data[i], &packet);

        if (ev == STREAM_EVENT_FRAME)
        {
            ++frames;
            if (last_packet != NULL)
            {
                *last_packet = packet;
            }
        }
        else if (ev == STREAM_EVENT_ERROR)
        {
            if (error_count != NULL)
            {
                ++(*error_count);
            }
        }
    }

    return frames;
}

static void test_byte_by_byte_frame(void)
{
    uint8_t frame[PROTOCOL_MAX_FRAME_SIZE] = {0};
    const size_t len =
        make_frame(MSG_PING, 100u, NULL, 0u, frame, sizeof(frame));

    stream_parser_t parser;
    protocol_packet_t packet = {0};
    int errors = 0;

    stream_parser_init(&parser);

    const int frames =
        feed_and_count_frames(&parser, frame, len, &packet, &errors);

    CHECK_TRUE(
        "Byte-by-byte PING frame assembly",
        (len > 0u) &&
        (frames == 1) &&
        (errors == 0) &&
        (packet.msg_id == MSG_PING) &&
        (packet.seq == 100u) &&
        (parser.frames_ok == 1u));
}

static void test_noise_before_magic(void)
{
    uint8_t frame[PROTOCOL_MAX_FRAME_SIZE] = {0};
    const size_t len =
        make_frame(MSG_READ_STATUS, 7u, NULL, 0u, frame, sizeof(frame));

    const uint8_t noise[] = {0x00u, 0x11u, 0x22u, 0x33u, 0x44u};

    stream_parser_t parser;
    protocol_packet_t packet = {0};
    int errors = 0;

    stream_parser_init(&parser);

    (void)feed_and_count_frames(
        &parser, noise, sizeof(noise), &packet, &errors);

    const int frames =
        feed_and_count_frames(&parser, frame, len, &packet, &errors);

    CHECK_TRUE(
        "Ignore noise before MAGIC",
        (frames == 1) &&
        (errors == 0) &&
        (packet.msg_id == MSG_READ_STATUS) &&
        (packet.seq == 7u) &&
        (parser.discarded_bytes >= sizeof(noise)));
}

static void test_magic_overlap(void)
{
    uint8_t frame[PROTOCOL_MAX_FRAME_SIZE] = {0};
    const size_t len =
        make_frame(MSG_PING, 9u, NULL, 0u, frame, sizeof(frame));

    /*
     * Prefix one extra AA:
     * AA | AA 55 ...
     *      ^^^^^ actual MAGIC
     */
    uint8_t stream[PROTOCOL_MAX_FRAME_SIZE + 1u] = {0};
    stream[0] = 0xAAu;
    memcpy(&stream[1], frame, len);

    stream_parser_t parser;
    protocol_packet_t packet = {0};
    int errors = 0;

    stream_parser_init(&parser);

    const int frames =
        feed_and_count_frames(
            &parser, stream, len + 1u, &packet, &errors);

    CHECK_TRUE(
        "MAGIC overlap AA AA 55 resynchronization",
        (frames == 1) &&
        (errors == 0) &&
        (packet.msg_id == MSG_PING) &&
        (packet.seq == 9u));
}

static void test_payload_contains_magic(void)
{
    const uint8_t payload[] =
        {0x10u, 0xAAu, 0x55u, 0x20u, 0x30u};

    uint8_t frame[PROTOCOL_MAX_FRAME_SIZE] = {0};
    const size_t len =
        make_frame(
            MSG_TELEMETRY,
            42u,
            payload,
            (uint16_t)sizeof(payload),
            frame,
            sizeof(frame));

    stream_parser_t parser;
    protocol_packet_t packet = {0};
    int errors = 0;

    stream_parser_init(&parser);

    const int frames =
        feed_and_count_frames(&parser, frame, len, &packet, &errors);

    CHECK_TRUE(
        "Payload AA55 is not treated as a new frame",
        (frames == 1) &&
        (errors == 0) &&
        (packet.length == sizeof(payload)) &&
        (memcmp(packet.payload, payload, sizeof(payload)) == 0));
}

static void test_two_concatenated_frames(void)
{
    uint8_t frame1[PROTOCOL_MAX_FRAME_SIZE] = {0};
    uint8_t frame2[PROTOCOL_MAX_FRAME_SIZE] = {0};

    const size_t len1 =
        make_frame(MSG_PING, 1u, NULL, 0u, frame1, sizeof(frame1));
    const size_t len2 =
        make_frame(MSG_PING, 2u, NULL, 0u, frame2, sizeof(frame2));

    stream_parser_t parser;
    protocol_packet_t packet = {0};
    int errors = 0;
    int frames = 0;

    stream_parser_init(&parser);

    frames +=
        feed_and_count_frames(&parser, frame1, len1, &packet, &errors);
    frames +=
        feed_and_count_frames(&parser, frame2, len2, &packet, &errors);

    CHECK_TRUE(
        "Two back-to-back frames",
        (frames == 2) &&
        (errors == 0) &&
        (packet.seq == 2u) &&
        (parser.frames_ok == 2u));
}

static void test_crc_error_then_recovery(void)
{
    uint8_t bad_frame[PROTOCOL_MAX_FRAME_SIZE] = {0};
    uint8_t good_frame[PROTOCOL_MAX_FRAME_SIZE] = {0};

    const size_t bad_len =
        make_frame(MSG_PING, 10u, NULL, 0u, bad_frame, sizeof(bad_frame));
    const size_t good_len =
        make_frame(MSG_PING, 11u, NULL, 0u, good_frame, sizeof(good_frame));

    /* Corrupt MSG_ID, leaving original CRC unchanged. */
    bad_frame[3] ^= 0x01u;

    stream_parser_t parser;
    protocol_packet_t packet = {0};
    int errors = 0;
    int frames = 0;

    stream_parser_init(&parser);

    frames +=
        feed_and_count_frames(
            &parser, bad_frame, bad_len, &packet, &errors);

    frames +=
        feed_and_count_frames(
            &parser, good_frame, good_len, &packet, &errors);

    CHECK_TRUE(
        "CRC error rejected and next frame recovered",
        (frames == 1) &&
        (errors == 1) &&
        (packet.seq == 11u) &&
        (parser.frames_ok == 1u) &&
        (parser.crc_errors == 1u));
}

static void test_invalid_version_then_recovery(void)
{
    uint8_t bad[PROTOCOL_MAX_FRAME_SIZE] = {0};
    uint8_t good[PROTOCOL_MAX_FRAME_SIZE] = {0};

    const size_t bad_len =
        make_frame(MSG_PING, 20u, NULL, 0u, bad, sizeof(bad));
    const size_t good_len =
        make_frame(MSG_PING, 21u, NULL, 0u, good, sizeof(good));

    bad[2] = 0x02u;

    stream_parser_t parser;
    protocol_packet_t packet = {0};
    int errors = 0;
    int frames = 0;

    stream_parser_init(&parser);

    frames +=
        feed_and_count_frames(&parser, bad, bad_len, &packet, &errors);
    frames +=
        feed_and_count_frames(&parser, good, good_len, &packet, &errors);

    CHECK_TRUE(
        "Invalid VERSION rejected and parser recovers",
        (frames == 1) &&
        (errors >= 1) &&
        (packet.seq == 21u) &&
        (parser.frames_ok == 1u) &&
        (parser.format_errors >= 1u));
}

static void test_oversized_length_then_recovery(void)
{
    /*
     * Feed a fake header whose LENGTH is 257.
     * Then feed a valid frame.
     */
    const uint8_t fake_header[PROTOCOL_FIXED_HEADER_SIZE] =
    {
        0xAAu, 0x55u,
        PROTOCOL_VERSION,
        MSG_PING,
        0x00u, 0x01u,
        0x01u, 0x01u
    };

    uint8_t good[PROTOCOL_MAX_FRAME_SIZE] = {0};
    const size_t good_len =
        make_frame(MSG_PING, 31u, NULL, 0u, good, sizeof(good));

    stream_parser_t parser;
    protocol_packet_t packet = {0};
    int errors = 0;
    int frames = 0;

    stream_parser_init(&parser);

    frames +=
        feed_and_count_frames(
            &parser,
            fake_header,
            sizeof(fake_header),
            &packet,
            &errors);

    frames +=
        feed_and_count_frames(
            &parser,
            good,
            good_len,
            &packet,
            &errors);

    CHECK_TRUE(
        "Oversized LENGTH rejected and parser recovers",
        (frames == 1) &&
        (errors >= 1) &&
        (packet.seq == 31u) &&
        (parser.frames_ok == 1u) &&
        (parser.format_errors >= 1u));
}

int main(void)
{
    printf("Protocol V1 Stream Parser Unit Test\n");
    printf("===================================\n");

    test_byte_by_byte_frame();
    test_noise_before_magic();
    test_magic_overlap();
    test_payload_contains_magic();
    test_two_concatenated_frames();
    test_crc_error_then_recovery();
    test_invalid_version_then_recovery();
    test_oversized_length_then_recovery();

    printf("\nResult: %d passed, %d failed\n", g_passed, g_failed);

    return (g_failed == 0) ? 0 : 1;
}