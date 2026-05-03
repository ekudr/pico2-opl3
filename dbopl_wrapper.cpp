#include "dbopl_wrapper.h"
#include "dbopl.h"
#include <new>

/* Single OPL3-mode chip instance, allocated in BSS. */
static DBOPL::Chip g_chip(true);

extern "C" {

void OPL3_Reset(opl3_chip *, uint32_t samplerate)
{
    /* Chip has a const member — use placement new to reconstruct in-place. */
    new (&g_chip) DBOPL::Chip(true);
    g_chip.Setup(samplerate);
}

void OPL3_WriteReg(opl3_chip *, uint16_t reg, uint8_t val)
{
    g_chip.WriteReg(reg, val);
}

/* Quadratic soft-knee: slope=1 below KNEE, smoothly rolls off to PEAK at LIMIT.
 * LIMIT = KNEE + 2*(PEAK-KNEE) ensures f(LIMIT)=PEAK with continuous derivative. */
static inline int32_t soft_clip(int32_t v) {
    static const int32_t KNEE  = 24000;
    static const int32_t PEAK  = 32767;
    static const int32_t LIMIT = KNEE + 2 * (PEAK - KNEE); /* 41534 */
    static const int32_t DENOM = 4 * (PEAK - KNEE);        /* 35068 */
    if (v >= -KNEE && v <= KNEE) return v;
    int32_t sign = (v >= 0) ? 1 : -1;
    int32_t x = v * sign;
    if (x >= LIMIT) return sign * PEAK;
    x -= KNEE;
    return sign * (KNEE + x - x * x / DENOM);
}

void OPL3_GenerateStream(opl3_chip *, int16_t *buf, uint32_t numsamples)
{
    /* GenerateBlock3 adds into the buffer — must zero first. */
    int32_t tmp[numsamples * 2];
    for (uint32_t i = 0; i < numsamples * 2; i++)
        tmp[i] = 0;

    g_chip.GenerateBlock3(numsamples, tmp);

    for (uint32_t i = 0; i < numsamples * 2; i++)
        buf[i] = (int16_t)soft_clip(tmp[i]);
}

} /* extern "C" */
