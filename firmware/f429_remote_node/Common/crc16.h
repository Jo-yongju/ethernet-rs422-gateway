#ifndef CRC16_H
#define CRC16_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * CRC-16/CCITT-FALSE
 *
 * Polynomial : 0x1021
 * Initial    : 0xFFFF
 * RefIn      : false
 * RefOut     : false
 * XorOut     : 0x0000
 *
 * Standard check:
 * "123456789" -> 0x29B1
 */
uint16_t crc16_ccitt_false(const uint8_t *data, size_t length);

#ifdef __cplusplus
}
#endif

#endif /* CRC16_H */
