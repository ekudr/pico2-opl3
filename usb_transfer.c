#include "usb_transfer.h"

#include <string.h>
#include <stdio.h>
#include "pico/stdlib.h"

bool usb_receive_vgm(uint8_t *buf, uint32_t buf_size, uint32_t *out_len)
{
    /* Read 4-byte magic */
    uint8_t magic[4];
    for (int i = 0; i < 4; i++) {
        int c = getchar_timeout_us(10000000u);  /* 10 s timeout */
        if (c == PICO_ERROR_TIMEOUT) return false;
        magic[i] = (uint8_t)c;
    }
    if (memcmp(magic, "OPL3", 4) != 0) return false;

    /* Read 4-byte file size (little-endian) */
    uint32_t file_size = 0;
    for (int i = 0; i < 4; i++) {
        int c = getchar_timeout_us(2000000u);
        if (c == PICO_ERROR_TIMEOUT) return false;
        file_size |= (uint32_t)(uint8_t)c << (i * 8);
    }
    if (file_size == 0 || file_size > buf_size) return false;

    printf("READY\n");
    stdio_flush();

    /* Receive file data */
    for (uint32_t i = 0; i < file_size; i++) {
        int c = getchar_timeout_us(5000000u);
        if (c == PICO_ERROR_TIMEOUT) return false;
        buf[i] = (uint8_t)c;
    }

    *out_len = file_size;
    printf("OK\n");
    stdio_flush();
    return true;
}
