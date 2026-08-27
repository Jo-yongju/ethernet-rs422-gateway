#include "stream_parser.h"

#include <string.h>

#define MAGIC_HI                0xAAu
#define MAGIC_LO                0x55u
#define OFFSET_VERSION          2u
#define OFFSET_LENGTH           6u

static uint16_t read_u16_be(const uint8_t *src)
{
    return (uint16_t)(((uint16_t)src[0] << 8) | (uint16_t)src[1]);
}

/*
 * Preserve a possible MAGIC prefix at the tail of a rejected header.
 *
 * Example:
 *   ... AA 55
 *       ^^^^^ could already be the next frame's MAGIC
 *
 * or:
 *   ... AA
 *       ^^ could be the first byte of the next MAGIC.
 */
static void resync_from_tail(stream_parser_t *parser)
{
    const size_t n = parser->frame_index;

    if ((n >= 2u) &&
        (parser->frame_buffer[n - 2u] == MAGIC_HI) &&
        (parser->frame_buffer[n - 1u] == MAGIC_LO))
    {
        parser->frame_buffer[0] = MAGIC_HI;
        parser->frame_buffer[1] = MAGIC_LO;
        parser->frame_index = 2u;
        parser->expected_frame_length = 0u;
        parser->state = STREAM_STATE_READ_HEADER;
        return;
    }

    if ((n >= 1u) && (parser->frame_buffer[n - 1u] == MAGIC_HI))
    {
        parser->frame_buffer[0] = MAGIC_HI;
        parser->frame_index = 1u;
        parser->expected_frame_length = 0u;
        parser->state = STREAM_STATE_WAIT_MAGIC_2;
        return;
    }

    parser->frame_index = 0u;
    parser->expected_frame_length = 0u;
    parser->state = STREAM_STATE_WAIT_MAGIC_1;
}

void stream_parser_init(stream_parser_t *parser)
{
    if (parser == NULL)
    {
        return;
    }

    memset(parser, 0, sizeof(*parser));
    parser->state = STREAM_STATE_WAIT_MAGIC_1;
    parser->last_error = PROTO_OK;
}

void stream_parser_reset_frame(stream_parser_t *parser)
{
    if (parser == NULL)
    {
        return;
    }

    parser->state = STREAM_STATE_WAIT_MAGIC_1;
    parser->frame_index = 0u;
    parser->expected_frame_length = 0u;
    parser->last_error = PROTO_OK;
}

stream_parser_event_t stream_parser_feed_byte(
    stream_parser_t *parser,
    uint8_t byte,
    protocol_packet_t *out_packet)
{
    if ((parser == NULL) || (out_packet == NULL))
    {
        return STREAM_EVENT_ERROR;
    }

    switch (parser->state)
    {
        case STREAM_STATE_WAIT_MAGIC_1:
        {
            if (byte == MAGIC_HI)
            {
                parser->frame_buffer[0] = byte;
                parser->frame_index = 1u;
                parser->state = STREAM_STATE_WAIT_MAGIC_2;
            }
            else
            {
                parser->discarded_bytes++;
            }

            return STREAM_EVENT_NONE;
        }

        case STREAM_STATE_WAIT_MAGIC_2:
        {
            if (byte == MAGIC_LO)
            {
                parser->frame_buffer[1] = byte;
                parser->frame_index = 2u;
                parser->state = STREAM_STATE_READ_HEADER;
            }
            else if (byte == MAGIC_HI)
            {
                /*
                 * AA AA 55:
                 * the second AA may be the actual first MAGIC byte.
                 */
                parser->frame_buffer[0] = MAGIC_HI;
                parser->frame_index = 1u;
                parser->discarded_bytes++;
            }
            else
            {
                parser->frame_index = 0u;
                parser->state = STREAM_STATE_WAIT_MAGIC_1;
                parser->discarded_bytes += 2u;
            }

            return STREAM_EVENT_NONE;
        }

        case STREAM_STATE_READ_HEADER:
        {
            if (parser->frame_index >= PROTOCOL_FIXED_HEADER_SIZE)
            {
                parser->last_error = PROTO_ERR_BUFFER;
                parser->format_errors++;
                stream_parser_reset_frame(parser);
                return STREAM_EVENT_ERROR;
            }

            parser->frame_buffer[parser->frame_index++] = byte;

            if (parser->frame_index < PROTOCOL_FIXED_HEADER_SIZE)
            {
                return STREAM_EVENT_NONE;
            }

            if (parser->frame_buffer[OFFSET_VERSION] != PROTOCOL_VERSION)
            {
                parser->last_error = PROTO_ERR_VERSION;
                parser->format_errors++;
                resync_from_tail(parser);
                return STREAM_EVENT_ERROR;
            }

            const uint16_t payload_length =
                read_u16_be(&parser->frame_buffer[OFFSET_LENGTH]);

            if (payload_length > PROTOCOL_MAX_PAYLOAD)
            {
                parser->last_error = PROTO_ERR_LENGTH;
                parser->format_errors++;
                resync_from_tail(parser);
                return STREAM_EVENT_ERROR;
            }

            parser->expected_frame_length =
                PROTOCOL_FIXED_HEADER_SIZE +
                (size_t)payload_length +
                PROTOCOL_CRC_SIZE;

            if (parser->expected_frame_length > PROTOCOL_MAX_FRAME_SIZE)
            {
                parser->last_error = PROTO_ERR_LENGTH;
                parser->format_errors++;
                resync_from_tail(parser);
                return STREAM_EVENT_ERROR;
            }

            parser->state = STREAM_STATE_READ_REST;
            return STREAM_EVENT_NONE;
        }

        case STREAM_STATE_READ_REST:
        {
            if (parser->frame_index >= PROTOCOL_MAX_FRAME_SIZE)
            {
                parser->last_error = PROTO_ERR_BUFFER;
                parser->format_errors++;
                stream_parser_reset_frame(parser);
                return STREAM_EVENT_ERROR;
            }

            parser->frame_buffer[parser->frame_index++] = byte;

            if (parser->frame_index < parser->expected_frame_length)
            {
                return STREAM_EVENT_NONE;
            }

            if (parser->frame_index > parser->expected_frame_length)
            {
                parser->last_error = PROTO_ERR_FRAME_SIZE;
                parser->format_errors++;
                stream_parser_reset_frame(parser);
                return STREAM_EVENT_ERROR;
            }

            const protocol_result_t result =
                protocol_decode(
                    parser->frame_buffer,
                    parser->frame_index,
                    out_packet);

            parser->last_error = result;

            if (result == PROTO_OK)
            {
                parser->frames_ok++;
                stream_parser_reset_frame(parser);
                return STREAM_EVENT_FRAME;
            }

            if (result == PROTO_ERR_CRC)
            {
                parser->crc_errors++;
            }
            else
            {
                parser->format_errors++;
            }

            /*
             * At this point a complete candidate frame boundary was consumed.
             * Restart MAGIC search for subsequent stream bytes.
             *
             * If LENGTH itself was corrupted but still <= MAX_PAYLOAD,
             * bytes from a following frame may have been consumed before CRC
             * fails. V1 accepts that trade-off; the parser resynchronizes on a
             * later MAGIC. This behavior is intentionally testable.
             */
            stream_parser_reset_frame(parser);
            parser->last_error = result;
            return STREAM_EVENT_ERROR;
        }

        default:
        {
            stream_parser_init(parser);
            return STREAM_EVENT_ERROR;
        }
    }
}
