#include "uart_rx_ring.h"

#include <stddef.h>

void uart_rx_ring_init(uart_rx_ring_t *ring)
{
    if (ring != NULL)
    {
        ring->write_index = 0u;
        ring->read_index = 0u;
    }
}

int uart_rx_ring_push_isr(uart_rx_ring_t *ring, uint8_t byte)
{
    uint16_t next_write;

    if (ring == NULL)
    {
        return 0;
    }

    next_write = (uint16_t)(ring->write_index + 1u);
    if (next_write >= UART_RX_RING_SIZE)
    {
        next_write = 0u;
    }

    if (next_write == ring->read_index)
    {
        return 0;
    }

    ring->storage[ring->write_index] = byte;
    ring->write_index = next_write;
    return 1;
}

int uart_rx_ring_pop(uart_rx_ring_t *ring, uint8_t *byte)
{
    uint16_t next_read;

    if ((ring == NULL) || (byte == NULL) ||
        (ring->read_index == ring->write_index))
    {
        return 0;
    }

    *byte = ring->storage[ring->read_index];
    next_read = (uint16_t)(ring->read_index + 1u);
    if (next_read >= UART_RX_RING_SIZE)
    {
        next_read = 0u;
    }

    ring->read_index = next_read;
    return 1;
}
