#include "remote_node_port.h"

#include "main.h"
#include "remote_node.h"
#include "uart_rx_ring.h"

#define REMOTE_NODE_UART_TX_TIMEOUT_MS 100u

extern UART_HandleTypeDef huart5;

static remote_node_t remote_node;
static uart_rx_ring_t rx_ring;
static uint8_t rx_byte;
static volatile uint32_t rx_overflow_pending;
static volatile uint32_t uart_error_pending;

static uint32_t port_get_time_ms(void *user_context)
{
    (void)user_context;
    return HAL_GetTick();
}

static int port_transmit(
    const uint8_t *frame,
    size_t frame_length,
    void *user_context)
{
    (void)user_context;

    return (HAL_UART_Transmit(
                &huart5,
                (uint8_t *)frame,
                (uint16_t)frame_length,
                REMOTE_NODE_UART_TX_TIMEOUT_MS) == HAL_OK)
        ? 0
        : -1;
}

int remote_node_port_init(void)
{
    if (huart5.Instance != UART5)
    {
        return -1;
    }

    uart_rx_ring_init(&rx_ring);
    remote_node_init(
        &remote_node,
        port_get_time_ms,
        port_transmit,
        NULL);

    if (HAL_UART_Receive_IT(&huart5, &rx_byte, 1u) != HAL_OK)
    {
        return -1;
    }

    return 0;
}

void remote_node_port_process(void)
{
    uint8_t byte;
    uint32_t overflow_count;
    uint32_t error_count;
    uint32_t primask;

    /* Transfer ISR counters atomically; application state stays task-owned. */
    primask = __get_PRIMASK();
    __disable_irq();
    overflow_count = rx_overflow_pending;
    error_count = uart_error_pending;
    rx_overflow_pending = 0u;
    uart_error_pending = 0u;
    if (primask == 0u)
    {
        __enable_irq();
    }

    while (overflow_count != 0u)
    {
        remote_node_note_rx_overflow(&remote_node);
        overflow_count--;
    }
    while (error_count != 0u)
    {
        remote_node_note_uart_error(&remote_node);
        error_count--;
    }

    while (uart_rx_ring_pop(&rx_ring, &byte) != 0)
    {
        remote_node_process_byte(&remote_node, byte);
    }

    remote_node_process_periodic(&remote_node);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *uart_handle)
{
    if (uart_handle == &huart5)
    {
        if (uart_rx_ring_push_isr(&rx_ring, rx_byte) == 0)
        {
            rx_overflow_pending++;
        }

        if (HAL_UART_Receive_IT(&huart5, &rx_byte, 1u) != HAL_OK)
        {
            uart_error_pending++;
        }
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart_handle)
{
    if (uart_handle == &huart5)
    {
        uart_error_pending++;
        (void)HAL_UART_Receive_IT(&huart5, &rx_byte, 1u);
    }
}
