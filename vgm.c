#include "vgm.h"
#include "shared.h"

#include <string.h>
#include "pico/stdlib.h"

/* VGM timing is 44100 Hz; OPL3 native rate is 49716 Hz.
 * Exact ratio: 49716/44100 = 1381/1225. */
static inline uint32_t vgm_to_opl3(uint32_t vgm_ticks)
{
    return (uint32_t)(((uint64_t)vgm_ticks * 1381u + 612u) / 1225u);
}

bool vgm_open(vgm_state_t *v, const uint8_t *buf, uint32_t len)
{
    if (len < 0x40u) return false;
    if (memcmp(buf, "Vgm ", 4) != 0) return false;

    /* Data offset field at 0x34 is relative to address 0x34. */
    uint32_t rel = *(const uint32_t *)(buf + 0x34u);
    uint32_t data_start = 0x34u + (rel ? rel : 0x0Cu);
    if (data_start < 0x40u) data_start = 0x40u;
    if (data_start >= len) return false;

    v->data               = buf + data_start;
    v->data_len           = len - data_start;
    v->pos                = 0;
    v->vgm_sample_pos     = 0;
    v->opl3_sample_target = 0;
    v->finished           = false;
    return true;
}

static void push_reg_write(uint32_t sample_time, uint16_t reg, uint8_t val)
{
    opl3_cmd_t cmd = {
        .opl3_sample_time = sample_time,
        .reg              = reg,
        .val              = val,
        ._pad             = 0,
    };
    while (!cmd_queue_push(&g_cmd_queue, &cmd))
        tight_loop_contents();
}

void vgm_step(vgm_state_t *v)
{
    if (v->finished) return;

    /* Busy-wait until Core 1 has generated enough samples for this target.
     * Use elapsed = generated - base to avoid the counter-reset race. */
    while ((g_opl3_samples_generated - g_opl3_sample_base) < v->opl3_sample_target)
        tight_loop_contents();

    while (!v->finished) {
        if (v->pos >= v->data_len) {
            v->finished      = true;
            g_playback_active = false;
            return;
        }

        uint8_t cmd = v->data[v->pos++];

        if (cmd == 0x5Eu || cmd == 0x5Au) {
            /* OPL3 port 0 / OPL2 chip 0: reg 0x000..0x0FF */
            uint8_t reg = v->data[v->pos++];
            uint8_t val = v->data[v->pos++];
            push_reg_write(v->opl3_sample_target, reg, val);

        } else if (cmd == 0x5Fu || cmd == 0x5Bu) {
            /* OPL3 port 1 / OPL2 chip 1: reg 0x100..0x1FF */
            uint8_t reg = v->data[v->pos++];
            uint8_t val = v->data[v->pos++];
            push_reg_write(v->opl3_sample_target, 0x100u | reg, val);

        } else if (cmd == 0x61u) {
            uint16_t wait = (uint16_t)v->data[v->pos] |
                            ((uint16_t)v->data[v->pos + 1] << 8);
            v->pos                += 2;
            v->vgm_sample_pos     += wait;
            v->opl3_sample_target  = vgm_to_opl3(v->vgm_sample_pos);
            return;

        } else if (cmd == 0x62u) {
            v->vgm_sample_pos     += 735u;
            v->opl3_sample_target  = vgm_to_opl3(v->vgm_sample_pos);
            return;

        } else if (cmd == 0x63u) {
            v->vgm_sample_pos     += 882u;
            v->opl3_sample_target  = vgm_to_opl3(v->vgm_sample_pos);
            return;

        } else if ((cmd & 0xF0u) == 0x70u) {
            /* Short wait: (cmd & 0x0F) + 1 samples */
            uint32_t wait          = (cmd & 0x0Fu) + 1u;
            v->vgm_sample_pos     += wait;
            v->opl3_sample_target  = vgm_to_opl3(v->vgm_sample_pos);
            return;

        } else if (cmd == 0x66u) {
            v->finished      = true;
            g_playback_active = false;
            return;

        } else {
            /* Skip data bytes for commands we don't need to handle.
             * Lengths from VGM 1.70 spec (only the common ones). */
            static const uint8_t skip_table[0x100] = {
                [0x30] = 1, [0x31] = 1, [0x32] = 1, [0x33] = 1,
                [0x34] = 1, [0x35] = 1, [0x36] = 1, [0x37] = 1,
                [0x38] = 1, [0x39] = 1, [0x3A] = 1, [0x3B] = 1,
                [0x3C] = 1, [0x3D] = 1, [0x3E] = 1, [0x3F] = 1,
                [0x40] = 1, [0x41] = 1, [0x42] = 1, [0x43] = 1,
                [0x44] = 1, [0x45] = 1, [0x46] = 1, [0x47] = 1,
                [0x48] = 1, [0x49] = 1, [0x4A] = 1, [0x4B] = 1,
                [0x4C] = 1, [0x4D] = 1, [0x4E] = 1, [0x4F] = 1,
                [0x50] = 1, [0x51] = 2, [0x52] = 2, [0x53] = 2,
                [0x54] = 2, [0x55] = 2, [0x56] = 2, [0x57] = 2,
                [0x58] = 2, [0x59] = 2, [0x5A] = 0, /* handled above */
                [0x5B] = 0, /* handled above */ [0x5C] = 2, [0x5D] = 2, [0x5E] = 0,
                [0x5F] = 0, /* handled above */
                [0x67] = 0, /* data block — variable length, needs special handling */
                [0x68] = 5,
                [0xA0] = 2, [0xA1] = 2, [0xA2] = 2, [0xA3] = 2,
                [0xA4] = 2, [0xA5] = 2, [0xA6] = 2, [0xA7] = 2,
                [0xA8] = 2, [0xA9] = 2, [0xAA] = 2, [0xAB] = 2,
                [0xAC] = 2, [0xAD] = 2, [0xAE] = 2, [0xAF] = 2,
                [0xB0] = 2, [0xB1] = 2, [0xB2] = 2, [0xB3] = 2,
                [0xB4] = 2, [0xB5] = 2, [0xB6] = 2, [0xB7] = 2,
                [0xB8] = 2, [0xB9] = 2, [0xBA] = 2, [0xBB] = 2,
                [0xBC] = 2, [0xBD] = 2, [0xBE] = 2, [0xBF] = 2,
                [0xC0] = 3, [0xC1] = 3, [0xC2] = 3, [0xC3] = 3,
                [0xC4] = 3, [0xC5] = 3, [0xC6] = 3, [0xC7] = 3,
                [0xC8] = 3, [0xC9] = 3, [0xCA] = 3, [0xCB] = 3,
                [0xCC] = 3, [0xCD] = 3, [0xCE] = 3, [0xCF] = 3,
                [0xD0] = 3, [0xD1] = 3, [0xD2] = 3, [0xD3] = 3,
                [0xD4] = 3, [0xD5] = 3, [0xD6] = 3,
                [0xE0] = 4, [0xE1] = 4,
            };

            if (cmd == 0x67u) {
                /* Data block: 0x67 0x66 tt ss ss ss ss <data> */
                v->pos += 2;  /* skip 0x66 and type byte */
                uint32_t block_size = *(const uint32_t *)(v->data + v->pos);
                v->pos += 4 + (block_size & 0x7FFFFFFFu);
            } else {
                uint8_t skip = skip_table[cmd];
                v->pos += skip;
            }
        }
    }
}
