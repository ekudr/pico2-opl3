#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handle — callers pass a pointer but dbopl state lives in a C++ global. */
typedef struct { int _unused; } opl3_chip;

void OPL3_Reset(opl3_chip *chip, uint32_t samplerate);
void OPL3_WriteReg(opl3_chip *chip, uint16_t reg, uint8_t val);
void OPL3_GenerateStream(opl3_chip *chip, int16_t *buf, uint32_t numsamples);

#ifdef __cplusplus
}
#endif
