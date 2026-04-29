#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    const uint8_t *data;              /* pointer to VGM data section */
    uint32_t       data_len;
    uint32_t       pos;               /* byte offset into data[] */
    uint32_t       vgm_sample_pos;    /* accumulated 44100 Hz ticks */
    uint32_t       opl3_sample_target; /* in OPL3-rate (49716 Hz) samples */
    bool           finished;
} vgm_state_t;

/* Parse VGM header and initialise state. Returns false on bad magic. */
bool vgm_open(vgm_state_t *v, const uint8_t *buf, uint32_t len);

/* Advance the parser: push register writes to the shared queue up to the
 * current timing target, then update the target to the next wait boundary.
 * Call in a loop from Core 0 while !v->finished. */
void vgm_step(vgm_state_t *v);
