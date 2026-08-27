#include "remote_node.h"

#include <string.h>

static void write_u16_be(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)((value >> 8) & 0xFFu);
    dst[1] = (uint8_t)(value & 0xFFu);
}

static void write_u32_be(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)((value >> 24) & 0xFFu);
    dst[1] = (uint8_t)((value >> 16) & 0xFFu);
    dst[2] = (uint8_t)((value >> 8) & 0xFFu);
    dst[3] = (uint8_t)(value & 0xFFu);
}

static uint32_t remote_node_now(const remote_node_t *node)
{
    if ((node == NULL) || (node->time_fn == NULL))
    {
        return 0u;
    }

    return node->time_fn(node->user_context);
}

static int remote_node_send_packet(
    remote_node_t *node,
    const protocol_packet_t *packet)
{
    uint8_t frame[PROTOCOL_MAX_FRAME_SIZE];
    size_t frame_length = 0u;

    if ((node == NULL) || (packet == NULL) || (node->tx_fn == NULL))
    {
        return -1;
    }

    if (protocol_encode(packet, frame, sizeof(frame), &frame_length) != PROTO_OK)
    {
        node->stats.tx_error_count++;
        node->stats.comm_status = COMM_ERROR;
        return -1;
    }

    if (node->tx_fn(frame, frame_length, node->user_context) != 0)
    {
        node->stats.tx_error_count++;
        node->stats.comm_status = COMM_ERROR;
        return -1;
    }

    return 0;
}

static void remote_node_send_error(
    remote_node_t *node,
    uint16_t request_seq,
    remote_node_error_code_t error_code,
    uint8_t offending_msg_id)
{
    protocol_packet_t response;

    memset(&response, 0, sizeof(response));
    response.version = PROTOCOL_VERSION;
    response.msg_id = MSG_ERROR;
    response.seq = request_seq;
    response.length = 2u;
    response.payload[0] = (uint8_t)error_code;
    response.payload[1] = offending_msg_id;

    (void)remote_node_send_packet(node, &response);
}

static void remote_node_remember_request(
    remote_node_t *node,
    const protocol_packet_t *request)
{
    if ((node->last_request_valid != 0u) &&
        (node->last_request_msg_id == request->msg_id) &&
        (node->last_request_seq == request->seq))
    {
        node->stats.duplicate_seq_count++;
    }

    node->last_request_msg_id = request->msg_id;
    node->last_request_seq = request->seq;
    node->last_request_valid = 1u;
}

static void remote_node_handle_ping(
    remote_node_t *node,
    const protocol_packet_t *request)
{
    protocol_packet_t response;

    if (request->length != 0u)
    {
        remote_node_send_error(
            node,
            request->seq,
            REMOTE_ERROR_BAD_LENGTH,
            request->msg_id);
        return;
    }

    remote_node_remember_request(node, request);

    memset(&response, 0, sizeof(response));
    response.version = PROTOCOL_VERSION;
    response.msg_id = MSG_PONG;
    response.seq = request->seq;
    response.length = 0u;

    (void)remote_node_send_packet(node, &response);
}

static void remote_node_handle_read_status(
    remote_node_t *node,
    const protocol_packet_t *request)
{
    protocol_packet_t response;

    if (request->length != 0u)
    {
        remote_node_send_error(
            node,
            request->seq,
            REMOTE_ERROR_BAD_LENGTH,
            request->msg_id);
        return;
    }

    remote_node_remember_request(node, request);

    memset(&response, 0, sizeof(response));
    response.version = PROTOCOL_VERSION;
    response.msg_id = MSG_STATUS_RESP;
    response.seq = request->seq;
    response.length = 20u;

    write_u32_be(&response.payload[0], remote_node_now(node));
    write_u32_be(&response.payload[4], node->stats.valid_frame_count);
    write_u32_be(&response.payload[8], node->stats.parser_error_count);
    write_u32_be(&response.payload[12], node->stats.crc_error_count);
    write_u16_be(&response.payload[16], node->stats.last_rx_seq);
    response.payload[18] = (uint8_t)node->stats.comm_status;
    response.payload[19] = 0u;

    (void)remote_node_send_packet(node, &response);
}

static void remote_node_handle_packet(
    remote_node_t *node,
    const protocol_packet_t *packet)
{
    node->stats.valid_frame_count++;
    node->stats.last_rx_seq = packet->seq;

    if (node->stats.comm_status == COMM_INIT)
    {
        node->stats.comm_status = COMM_OK;
    }

    switch (packet->msg_id)
    {
        case MSG_PING:
            remote_node_handle_ping(node, packet);
            break;

        case MSG_READ_STATUS:
            remote_node_handle_read_status(node, packet);
            break;

        case MSG_PONG:
        case MSG_STATUS_RESP:
        case MSG_HEARTBEAT:
        case MSG_TELEMETRY:
            remote_node_send_error(
                node,
                packet->seq,
                REMOTE_ERROR_UNSUPPORTED,
                packet->msg_id);
            break;

        case MSG_ERROR:
            /* Never answer ERROR with ERROR; that can create an error loop. */
            break;

        default:
            node->stats.unknown_msg_count++;
            remote_node_send_error(
                node,
                packet->seq,
                REMOTE_ERROR_UNKNOWN_MSG,
                packet->msg_id);
            break;
    }
}

void remote_node_init(
    remote_node_t *node,
    remote_node_time_fn_t time_fn,
    remote_node_tx_fn_t tx_fn,
    void *user_context)
{
    uint32_t now;

    if (node == NULL)
    {
        return;
    }

    memset(node, 0, sizeof(*node));
    stream_parser_init(&node->parser);
    node->time_fn = time_fn;
    node->tx_fn = tx_fn;
    node->user_context = user_context;
    node->stats.comm_status = COMM_INIT;

    now = remote_node_now(node);
    node->last_heartbeat_ms = now;
    node->last_telemetry_ms = now;
}

void remote_node_process_byte(remote_node_t *node, uint8_t byte)
{
    protocol_packet_t packet;
    stream_parser_event_t event;

    if (node == NULL)
    {
        return;
    }

    event = stream_parser_feed_byte(&node->parser, byte, &packet);

    if (event == STREAM_EVENT_FRAME)
    {
        remote_node_handle_packet(node, &packet);
    }
    else if (event == STREAM_EVENT_ERROR)
    {
        if (node->parser.last_error == PROTO_ERR_CRC)
        {
            node->stats.crc_error_count++;
        }
        else
        {
            node->stats.parser_error_count++;
        }
    }
}

void remote_node_process_bytes(
    remote_node_t *node,
    const uint8_t *bytes,
    size_t length)
{
    size_t i;

    if ((node == NULL) || ((bytes == NULL) && (length != 0u)))
    {
        return;
    }

    for (i = 0u; i < length; ++i)
    {
        remote_node_process_byte(node, bytes[i]);
    }
}

void remote_node_process_periodic(remote_node_t *node)
{
    protocol_packet_t packet;
    uint32_t now;

    if (node == NULL)
    {
        return;
    }

    now = remote_node_now(node);

    if ((uint32_t)(now - node->last_telemetry_ms) >=
        REMOTE_NODE_TELEMETRY_PERIOD_MS)
    {
        node->last_telemetry_ms = now;
        node->telemetry_seq = (uint16_t)(node->telemetry_seq + 1u);

        memset(&packet, 0, sizeof(packet));
        packet.version = PROTOCOL_VERSION;
        packet.msg_id = MSG_TELEMETRY;
        packet.seq = node->telemetry_seq;
        packet.length = 6u;
        write_u32_be(&packet.payload[0], now);
        write_u16_be(&packet.payload[4], node->status_flags);
        (void)remote_node_send_packet(node, &packet);
    }

    if ((uint32_t)(now - node->last_heartbeat_ms) >=
        REMOTE_NODE_HEARTBEAT_PERIOD_MS)
    {
        node->last_heartbeat_ms = now;
        node->heartbeat_seq = (uint16_t)(node->heartbeat_seq + 1u);

        memset(&packet, 0, sizeof(packet));
        packet.version = PROTOCOL_VERSION;
        packet.msg_id = MSG_HEARTBEAT;
        packet.seq = node->heartbeat_seq;
        packet.length = 4u;
        write_u32_be(&packet.payload[0], now);
        (void)remote_node_send_packet(node, &packet);
    }
}

void remote_node_set_status_flags(remote_node_t *node, uint16_t status_flags)
{
    if (node != NULL)
    {
        node->status_flags = status_flags;
    }
}

void remote_node_note_rx_overflow(remote_node_t *node)
{
    if (node != NULL)
    {
        node->stats.uart_rx_overflow_count++;
        node->stats.comm_status = COMM_ERROR;
    }
}

void remote_node_note_uart_error(remote_node_t *node)
{
    if (node != NULL)
    {
        node->stats.uart_error_count++;
        node->stats.comm_status = COMM_ERROR;
    }
}

const remote_node_stats_t *remote_node_get_stats(const remote_node_t *node)
{
    return (node != NULL) ? &node->stats : NULL;
}
