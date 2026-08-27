#ifndef UART_RX_RING_H
#define UART_RX_RING_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UART_RX_RING_SIZE 512u

typedef struct
{
    uint8_t storage[UART_RX_RING_SIZE];
    volatile uint16_t write_index;
    volatile uint16_t read_index;
} uart_rx_ring_t;

void uart_rx_ring_init(uart_rx_ring_t *ring);
int uart_rx_ring_push_isr(uart_rx_ring_t *ring, uint8_t byte);
int uart_rx_ring_pop(uart_rx_ring_t *ring, uint8_t *byte);

#ifdef __cplusplus
}
#endif

#endif /* UART_RX_RING_H */
