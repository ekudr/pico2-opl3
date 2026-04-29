#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "hardware/sync.h"

/* OPL3 register write command, timestamped in OPL3-rate samples (49716 Hz). */
typedef struct {
    uint32_t opl3_sample_time;
    uint16_t reg;
    uint8_t  val;
    uint8_t  _pad;
} opl3_cmd_t;

/* Lock-free single-producer / single-consumer ring buffer.
 * Producer = Core 0 (VGM parser), Consumer = Core 1 (DMA ISR). */
#define CMD_QUEUE_SIZE 64u

typedef struct {
    opl3_cmd_t        buf[CMD_QUEUE_SIZE];
    volatile uint32_t head;  /* written by producer */
    volatile uint32_t tail;  /* written by consumer */
} cmd_queue_t;

extern cmd_queue_t       g_cmd_queue;
/* Absolute OPL3-rate sample counter; updated by Core 1 DMA ISR. */
extern volatile uint32_t g_opl3_samples_generated;
/* Snapshot of g_opl3_samples_generated taken at playback start.
 * All VGM timestamps are relative to this baseline to avoid a reset race. */
extern volatile uint32_t g_opl3_sample_base;
/* Set true by Core 0 before playback; cleared by Core 1 on end-of-data. */
extern volatile bool     g_playback_active;
/* Set true by Core 0 to request an OPL3 chip reset from Core 1. */
extern volatile bool     g_reset_opl3;

static inline bool cmd_queue_push(cmd_queue_t *q, const opl3_cmd_t *cmd)
{
    uint32_t head = q->head;
    uint32_t next = (head + 1u) & (CMD_QUEUE_SIZE - 1u);
    if (next == q->tail) return false;  /* full */
    q->buf[head] = *cmd;
    __dmb();
    q->head = next;
    return true;
}

static inline bool cmd_queue_peek(cmd_queue_t *q, opl3_cmd_t *out)
{
    if (q->tail == q->head) return false;  /* empty */
    __dmb();
    *out = q->buf[q->tail];
    return true;
}

static inline void cmd_queue_pop(cmd_queue_t *q)
{
    __dmb();
    q->tail = (q->tail + 1u) & (CMD_QUEUE_SIZE - 1u);
}
