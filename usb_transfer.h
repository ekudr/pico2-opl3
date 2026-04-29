#pragma once

#include <stdint.h>
#include <stdbool.h>

/* Block until a VGM file is received over USB CDC.
 * Protocol: magic "OPL3" (4 bytes) + file_size (4 bytes LE)
 *           → Pico: "READY\n"
 *           PC streams file_size bytes
 *           → Pico: "OK\n"
 * Returns false on timeout or invalid magic/size. */
bool usb_receive_vgm(uint8_t *buf, uint32_t buf_size, uint32_t *out_len);
