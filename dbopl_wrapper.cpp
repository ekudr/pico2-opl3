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

void OPL3_GenerateStream(opl3_chip *, int16_t *buf, uint32_t numsamples)
{
    /* GenerateBlock3 adds into the buffer — must zero first. */
    int32_t tmp[numsamples * 2];
    for (uint32_t i = 0; i < numsamples * 2; i++)
        tmp[i] = 0;

    g_chip.GenerateBlock3(numsamples, tmp);

    for (uint32_t i = 0; i < numsamples * 2; i++) {
        int32_t v = tmp[i];
        if (v > 32767)  v = 32767;
        if (v < -32768) v = -32768;
        buf[i] = (int16_t)v;
    }
}

} /* extern "C" */
