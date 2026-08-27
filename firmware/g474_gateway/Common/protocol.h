#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PROTOCOL_MAGIC               0xAA55u
#define PROTOCOL_VERSION             0x01u
#define PROTOCOL_MAX_PAYLOAD         256u

#define PROTOCOL_FIXED_HEADER_SIZE   8u
#define PROTOCOL_CRC_SIZE            2u
#define PROTOCOL_MIN_FRAME_SIZE      (PROTOCOL_FIXED_HEADER_SIZE + PROTOCOL_CRC_SIZE)
#define PROTOCOL_MAX_FRAME_SIZE      (PROTOCOL_FIXED_HEADER_SIZE + PROTOCOL_MAX_PAYLOAD + PROTOCOL_CRC_SIZE)

/* Message IDs */
#define MSG_PING                     0x01u
#define MSG_PONG                     0x81u
#define MSG_READ_STATUS              0x10u
#define MSG_STATUS_RESP              0x90u
#define MSG_HEARTBEAT                0x20u
#define MSG_TELEMETRY                0x40u
#define MSG_ERROR                    0xE0u

typedef enum
{
    PROTO_OK = 0,
    PROTO_ERR_NULL = -1,
    PROTO_ERR_BUFFER = -2,
    PROTO_ERR_MAGIC = -3,
    PROTO_ERR_VERSION = -4,
    PROTO_ERR_LENGTH = -5,
    PROTO_ERR_FRAME_SIZE = -6,
    PROTO_ERR_CRC = -7
} protocol_result_t;

/*
 * Logical packet representation.
 *
 * IMPORTANT:
 * Do NOT transmit this struct memory directly.
 * protocol_encode()/protocol_decode() explicitly serialize fields
 * using the Protocol V1 Big Endian wire format.
 */
typedef struct
{
    uint8_t  version;
    uint8_t  msg_id;
    uint16_t seq;
    uint16_t length;
    uint8_t  payload[PROTOCOL_MAX_PAYLOAD];
} protocol_packet_t;

/**
 * Serialize one logical packet into one complete Protocol V1 frame.
 *
 * Wire format:
 * MAGIC(2) | VERSION(1) | MSG_ID(1) | SEQ(2) |
 * LENGTH(2) | PAYLOAD(N) | CRC16(2)
 *
 * Multi-byte fields use Big Endian.
 *
 * CRC range:
 * VERSION | MSG_ID | SEQ | LENGTH | PAYLOAD
 *
 * MAGIC and the CRC field itself are excluded from the CRC calculation.
 */
protocol_result_t protocol_encode(
    const protocol_packet_t *packet,
    uint8_t *buffer,
    size_t buffer_size,
    size_t *encoded_length
);

/**
 * Decode and validate exactly one complete Protocol V1 frame.
 *
 * This function is a complete-frame decoder, not a TCP/UART stream parser.
 * A stream parser should first collect one full frame using
 * MAGIC + fixed header + LENGTH and then call protocol_decode().
 */
protocol_result_t protocol_decode(
    const uint8_t *buffer,
    size_t buffer_length,
    protocol_packet_t *packet
);

const char *protocol_result_string(protocol_result_t result);

#ifdef __cplusplus
}
#endif

#endif /* PROTOCOL_H */