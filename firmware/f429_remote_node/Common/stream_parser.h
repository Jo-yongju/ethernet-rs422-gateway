#ifndef STREAM_PARSER_H
#define STREAM_PARSER_H

#include <stddef.h>
#include <stdint.h>

#include "protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    STREAM_STATE_WAIT_MAGIC_1 = 0,
    STREAM_STATE_WAIT_MAGIC_2,
    STREAM_STATE_READ_HEADER,
    STREAM_STATE_READ_REST
} stream_parser_state_t;

typedef enum
{
    STREAM_EVENT_ERROR = -1,
    STREAM_EVENT_NONE = 0,
    STREAM_EVENT_FRAME = 1
} stream_parser_event_t;

typedef struct
{
    stream_parser_state_t state;

    uint8_t frame_buffer[PROTOCOL_MAX_FRAME_SIZE];
    size_t frame_index;
    size_t expected_frame_length;

    protocol_result_t last_error;

    uint32_t frames_ok;
    uint32_t crc_errors;
    uint32_t format_errors;
    uint32_t discarded_bytes;
} stream_parser_t;

/**
 * Initialize parser state and statistics.
 */
void stream_parser_init(stream_parser_t *parser);

/**
 * Reset only the current in-progress frame state.
 * Statistics are preserved.
 */
void stream_parser_reset_frame(stream_parser_t *parser);

/**
 * Feed exactly one byte from TCP/UART stream.
 *
 * Return:
 *   STREAM_EVENT_NONE  : more bytes are needed
 *   STREAM_EVENT_FRAME : one complete valid frame was decoded into out_packet
 *   STREAM_EVENT_ERROR : one candidate frame was rejected
 *
 * Typical usage:
 *
 * for (size_t i = 0; i < rx_len; ++i)
 * {
 *     protocol_packet_t packet;
 *     stream_parser_event_t ev =
 *         stream_parser_feed_byte(&parser, rx_buf[i], &packet);
 *
 *     if (ev == STREAM_EVENT_FRAME)
 *     {
 *         handle_packet(&packet);
 *     }
 * }
 */
stream_parser_event_t stream_parser_feed_byte(
    stream_parser_t *parser,
    uint8_t byte,
    protocol_packet_t *out_packet
);

#ifdef __cplusplus
}
#endif

#endif /* STREAM_PARSER_H */
