#pragma once

#include <stdint.h>

/* GPIO16/17 → PWM slice 0, channels A/B on RP2350 ((16>>1)&7 = 0). */
#define AUDIO_GPIO_LEFT   16u  /* PWM slice 0, channel A */
#define AUDIO_GPIO_RIGHT  17u  /* PWM slice 0, channel B */
#define AUDIO_PWM_SLICE   0u
#define AUDIO_BUF_SAMPLES 512u  /* samples per half-buffer; 512/49717 ≈ 10.3 ms ISR budget */

void audio_pwm_init(void);
/* Called from Core 1 after DMA IRQ is registered. Starts DMA chain. */
void audio_dma_start(void);
/* Core 1 entry point — sets up DMA_IRQ_1 on Core 1's NVIC, starts audio. */
void core1_audio_entry(void);
