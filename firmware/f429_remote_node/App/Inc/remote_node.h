#ifndef REMOTE_NODE_H
#define REMOTE_NODE_H

#include <stddef.h>
#include <stdint.h>

#include "stream_parser.h"

#ifdef __cplusplus
extern "C" {
#endif

#define REMOTE_NODE_HEARTBEAT_PERIOD_MS  500u
#define REMOTE_NODE_TELEMETRY_PERIOD_MS  100u

typedef enum
{
    COMM_INIT = 0,
    COMM_OK = 1,
    COMM_ERROR = 2
} remote_node_comm_status_t;

typedef enum
{
    REMOTE_ERROR_UNKNOWN_MSG = 1,
    REMOTE_ERROR_BAD_LENGTH = 2,
    REMOTE_ERROR_UNSUPPORTED = 3
} remote_node_error_code_t;

typedef uint32_t (*remote_node_time_fn_t)(void *user_context);
typedef int (*remote_node_tx_fn_t)(
    const uint8_t *frame,
    size_t frame_length,
    void *user_context);

typedef struct
{
    /* Incremented only after MAGIC, VERSION, LENGTH, and CRC validate. */
    uint32_t valid_frame_count;
    /* Non-CRC structural parser rejections (version, length, framing, etc.). */
    uint32_t parser_error_count;
    /* Frames rejected specifically because Protocol V1 CRC validation failed. */
    uint32_t crc_error_count;
    /* Structurally valid frames with an unassigned message ID. */
    uint32_t unknown_msg_count;
    /* Consecutive PING/READ_STATUS requests with the same MSG_ID and SEQ. */
    uint32_t duplicate_seq_count;
    /* Bytes dropped because the interrupt RX ring was full. */
    uint32_t uart_rx_overflow_count;
    /* UART hardware errors reported by HAL (overrun, framing, noise, parity). */
    uint32_t uart_error_count;
    /* Local transmit failures reported by the platform adapter. */
    uint32_t tx_error_count;

    uint16_t last_rx_seq;
    remote_node_comm_status_t comm_status;
} remote_node_stats_t;

typedef struct
{
    stream_parser_t parser;
    remote_node_stats_t stats;

    remote_node_time_fn_t time_fn;
    remote_node_tx_fn_t tx_fn;
    void *user_context;

    uint32_t last_heartbeat_ms;
    uint32_t last_telemetry_ms;
    uint16_t heartbeat_seq;
    uint16_t telemetry_seq;
    uint16_t status_flags;

    uint8_t last_request_msg_id;
    uint16_t last_request_seq;
    uint8_t last_request_valid;
} remote_node_t;

void remote_node_init(
    remote_node_t *node,
    remote_node_time_fn_t time_fn,
    remote_node_tx_fn_t tx_fn,
    void *user_context);

void remote_node_process_byte(remote_node_t *node, uint8_t byte);
void remote_node_process_bytes(
    remote_node_t *node,
    const uint8_t *bytes,
    size_t length);
void remote_node_process_periodic(remote_node_t *node);

void remote_node_set_status_flags(remote_node_t *node, uint16_t status_flags);
void remote_node_note_rx_overflow(remote_node_t *node);
void remote_node_note_uart_error(remote_node_t *node);

const remote_node_stats_t *remote_node_get_stats(const remote_node_t *node);

#ifdef __cplusplus
}
#endif

#endif /* REMOTE_NODE_H */
