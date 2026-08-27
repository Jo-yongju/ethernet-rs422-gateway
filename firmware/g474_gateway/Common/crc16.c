#include "crc16.h"

uint16_t crc16_ccitt_false(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFFu;

    if ((data == NULL) && (length != 0u))
    {
        return 0u;
    }

    for (size_t i = 0u; i < length; ++i)
    {
        crc ^= (uint16_t)data[i] << 8;

        for (uint8_t bit = 0u; bit < 8u; ++bit)
        {
            if ((crc & 0x8000u) != 0u)
            {
                crc = (uint16_t)((crc << 1) ^ 0x1021u);
            }
            else
            {
                crc = (uint16_t)(crc << 1);
            }
        }
    }

    return crc;
}