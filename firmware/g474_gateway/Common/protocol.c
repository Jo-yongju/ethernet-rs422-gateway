#include "protocol.h"
#include "crc16.h"

#include <string.h>

#define OFFSET_MAGIC        0u
#define OFFSET_VERSION      2u
#define OFFSET_MSG_ID       3u
#define OFFSET_SEQ          4u
#define OFFSET_LENGTH       6u
#define OFFSET_PAYLOAD      8u

static void write_u16_be(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)((value >> 8) & 0xFFu);
    dst[1] = (uint8_t)(value & 0xFFu);
}

static uint16_t read_u16_be(const uint8_t *src)
{
    return (uint16_t)(((uint16_t)src[0] << 8) | (uint16_t)src[1]);
}

protocol_result_t protocol_encode(
    const protocol_packet_t *packet,
    uint8_t *buffer,
    size_t buffer_size,
    size_t *encoded_length)
{
    if ((packet == NULL) || (buffer == NULL) || (encoded_length == NULL))
    {
        return PROTO_ERR_NULL;
    }

    *encoded_length = 0u;

    if (packet->version != PROTOCOL_VERSION)
    {
        return PROTO_ERR_VERSION;
    }

    if (packet->length > PROTOCOL_MAX_PAYLOAD)
    {
        return PROTO_ERR_LENGTH;
    }

    const size_t frame_length =
        PROTOCOL_FIXED_HEADER_SIZE +
        (size_t)packet->length +
        PROTOCOL_CRC_SIZE;

    if (buffer_size < frame_length)
    {
        return PROTO_ERR_BUFFER;
    }

    /* MAGIC */
    write_u16_be(&buffer[OFFSET_MAGIC], PROTOCOL_MAGIC);

    /* Header */
    buffer[OFFSET_VERSION] = packet->version;
    buffer[OFFSET_MSG_ID] = packet->msg_id;
    write_u16_be(&buffer[OFFSET_SEQ], packet->seq);
    write_u16_be(&buffer[OFFSET_LENGTH], packet->length);

    /* Payload */
    if (packet->length > 0u)
    {
        memcpy(&buffer[OFFSET_PAYLOAD], packet->payload, packet->length);
    }

    /*
     * CRC range:
     * VERSION(1) + MSG_ID(1) + SEQ(2) + LENGTH(2) + PAYLOAD(N)
     */
    const size_t crc_input_length = 6u + (size_t)packet->length;
    const uint16_t crc =
        crc16_ccitt_false(&buffer[OFFSET_VERSION], crc_input_length);

    const size_t crc_offset = OFFSET_PAYLOAD + (size_t)packet->length;
    write_u16_be(&buffer[crc_offset], crc);

    *encoded_length = frame_length;
    return PROTO_OK;
}

protocol_result_t protocol_decode(
    const uint8_t *buffer,
    size_t buffer_length,
    protocol_packet_t *packet)
{
    if ((buffer == NULL) || (packet == NULL))
    {
        return PROTO_ERR_NULL;
    }

    if (buffer_length < PROTOCOL_MIN_FRAME_SIZE)
    {
        return PROTO_ERR_BUFFER;
    }

    if (read_u16_be(&buffer[OFFSET_MAGIC]) != PROTOCOL_MAGIC)
    {
        return PROTO_ERR_MAGIC;
    }

    if (buffer[OFFSET_VERSION] != PROTOCOL_VERSION)
    {
        return PROTO_ERR_VERSION;
    }

    const uint16_t payload_length = read_u16_be(&buffer[OFFSET_LENGTH]);

    if (payload_length > PROTOCOL_MAX_PAYLOAD)
    {
        return PROTO_ERR_LENGTH;
    }

    const size_t expected_frame_length =
        PROTOCOL_FIXED_HEADER_SIZE +
        (size_t)payload_length +
        PROTOCOL_CRC_SIZE;

    if (buffer_length != expected_frame_length)
    {
        return PROTO_ERR_FRAME_SIZE;
    }

    const size_t crc_input_length = 6u + (size_t)payload_length;
    const uint16_t calculated_crc =
        crc16_ccitt_false(&buffer[OFFSET_VERSION], crc_input_length);

    const size_t crc_offset = OFFSET_PAYLOAD + (size_t)payload_length;
    const uint16_t received_crc = read_u16_be(&buffer[crc_offset]);

    if (calculated_crc != received_crc)
    {
        return PROTO_ERR_CRC;
    }

    packet->version = buffer[OFFSET_VERSION];
    packet->msg_id = buffer[OFFSET_MSG_ID];
    packet->seq = read_u16_be(&buffer[OFFSET_SEQ]);
    packet->length = payload_length;

    if (payload_length > 0u)
    {
        memcpy(packet->payload, &buffer[OFFSET_PAYLOAD], payload_length);
    }

    return PROTO_OK;
}

const char *protocol_result_string(protocol_result_t result)
{
    switch (result)
    {
        case PROTO_OK:             return "PROTO_OK";
        case PROTO_ERR_NULL:       return "PROTO_ERR_NULL";
        case PROTO_ERR_BUFFER:     return "PROTO_ERR_BUFFER";
        case PROTO_ERR_MAGIC:      return "PROTO_ERR_MAGIC";
        case PROTO_ERR_VERSION:    return "PROTO_ERR_VERSION";
        case PROTO_ERR_LENGTH:     return "PROTO_ERR_LENGTH";
        case PROTO_ERR_FRAME_SIZE: return "PROTO_ERR_FRAME_SIZE";
        case PROTO_ERR_CRC:        return "PROTO_ERR_CRC";
        default:                   return "PROTO_ERR_UNKNOWN";
    }
}