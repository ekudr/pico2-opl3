#include "audio_pwm.h"
#include "shared.h"
#include "dbopl_wrapper.h"

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/pwm.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/gpio.h"

/* PWM wrap for ~49717 Hz at 150 MHz system clock (RP2350 default).
 * 150 000 000 / 3017 = 49 717 Hz  (0.02 ppm from OPL3 native 49716 Hz). */
#define PWM_WRAP       3016u
#define PWM_DIV_INT    1u
#define PWM_DIV_FRAC4  0u

#define PWM_MID         ((PWM_WRAP + 1u) / 2u)
#define PWM_MID_STEREO  (PWM_MID | (PWM_MID << 16))

/* OPL3 register writes are applied in CHUNK_SIZE-sample steps for timing accuracy. */
#define CHUNK_SIZE 32u

/* 32-bit buffer: bits[15:0] = PWM channel A compare (left, GPIO16),
 *                bits[31:16] = PWM channel B compare (right, GPIO17).
 * 32-bit DMA avoids narrow-write undefined behaviour on the RP2350 AHB bus
 * and updates both channels atomically on the next PWM wrap. */
static uint32_t g_audio_buf[2][AUDIO_BUF_SAMPLES];
static int16_t  g_opl3_stereo[CHUNK_SIZE * 2];

static opl3_chip g_chip;
static uint      g_dma_ch[2];

static void __not_in_flash_func(audio_fill_buffer)(uint buf_idx)
{
    /* Per-channel fractional carry for first-order noise shaping.
     * Independent accumulators keep quantization noise decorrelated between
     * left and right — no patterned distortion, no inter-channel artefacts. */
    static uint32_t s_frac_l = 0;
    static uint32_t s_frac_r = 0;

    uint32_t *out     = g_audio_buf[buf_idx];
    uint32_t  cur     = g_opl3_samples_generated;
    uint32_t  elapsed = cur - g_opl3_sample_base;

    for (uint i = 0; i < AUDIO_BUF_SAMPLES; i += CHUNK_SIZE) {
        uint32_t chunk_end = elapsed + i + CHUNK_SIZE;

        if (g_playback_active) {
            opl3_cmd_t cmd;
            while (cmd_queue_peek(&g_cmd_queue, &cmd) &&
                   cmd.opl3_sample_time <= chunk_end) {
                cmd_queue_pop(&g_cmd_queue);
                OPL3_WriteReg(&g_chip, cmd.reg, cmd.val);
            }

            OPL3_GenerateStream(&g_chip, g_opl3_stereo, CHUNK_SIZE);

            for (uint j = 0; j < CHUNK_SIZE; j++) {
                int32_t L = g_opl3_stereo[j * 2];
                int32_t R = g_opl3_stereo[j * 2 + 1];

                /* Scale signed 16-bit → full PWM range (0..PWM_WRAP).
                 * u * (PWM_WRAP+1) / 65536, carrying the fractional bits
                 * forward to the next sample instead of discarding them. */
                uint32_t sl = (uint32_t)(L + 32768) * (PWM_WRAP + 1u) + s_frac_l;
                uint32_t pl = sl >> 16;
                s_frac_l    = sl & 0xFFFFu;
                if (pl > PWM_WRAP) pl = PWM_WRAP;

                uint32_t sr = (uint32_t)(R + 32768) * (PWM_WRAP + 1u) + s_frac_r;
                uint32_t pr = sr >> 16;
                s_frac_r    = sr & 0xFFFFu;
                if (pr > PWM_WRAP) pr = PWM_WRAP;

                out[i + j] = pl | (pr << 16);
            }
        } else {
            for (uint j = 0; j < CHUNK_SIZE; j++)
                out[i + j] = PWM_MID_STEREO;  /* silence at midpoint, both channels */
        }
    }

    __dmb();
    g_opl3_samples_generated += AUDIO_BUF_SAMPLES;
}


static void __isr dma_irq1_handler(void)
{
    uint completed;
    if (dma_irqn_get_channel_status(1, g_dma_ch[0])) {
        dma_irqn_acknowledge_channel(1, g_dma_ch[0]);
        dma_channel_set_read_addr(g_dma_ch[0], g_audio_buf[0],false);
        completed = 0;
    } else {
        dma_irqn_acknowledge_channel(1, g_dma_ch[1]);
        dma_channel_set_read_addr(g_dma_ch[1], g_audio_buf[1],false);
        completed = 1;
    }

    audio_fill_buffer(completed);
}

void audio_pwm_init(void)
{
    gpio_set_function(AUDIO_GPIO_LEFT,  GPIO_FUNC_PWM);
    gpio_set_drive_strength(AUDIO_GPIO_LEFT,  GPIO_DRIVE_STRENGTH_12MA);
    gpio_set_function(AUDIO_GPIO_RIGHT, GPIO_FUNC_PWM);
    gpio_set_drive_strength(AUDIO_GPIO_RIGHT, GPIO_DRIVE_STRENGTH_12MA);

    /* Both pins map to the same slice; channels A and B share the counter. */
    uint slice_num = pwm_gpio_to_slice_num(AUDIO_GPIO_LEFT);
    pwm_config cfg = pwm_get_default_config();

    pwm_config_set_wrap(&cfg, PWM_WRAP);
    pwm_config_set_clkdiv_int_frac4(&cfg, PWM_DIV_INT, PWM_DIV_FRAC4);
    pwm_init(slice_num, &cfg, true);

    pwm_set_gpio_level(AUDIO_GPIO_LEFT,  PWM_MID);
    pwm_set_gpio_level(AUDIO_GPIO_RIGHT, PWM_MID);

    g_dma_ch[0] = dma_claim_unused_channel(true);
    g_dma_ch[1] = dma_claim_unused_channel(true);

    /* Channel 0: buf[0] → PWM CC, chains to ch1 on completion */
    dma_channel_config c0 = dma_channel_get_default_config(g_dma_ch[0]);
    channel_config_set_transfer_data_size(&c0, DMA_SIZE_32);
    channel_config_set_read_increment(&c0, true);
    channel_config_set_write_increment(&c0, false);
    channel_config_set_dreq(&c0, pwm_get_dreq(slice_num));
    channel_config_set_chain_to(&c0, g_dma_ch[1]);
    dma_channel_configure(g_dma_ch[0], &c0,
        &pwm_hw->slice[slice_num].cc,
        g_audio_buf[0],
        AUDIO_BUF_SAMPLES,
        false);

    /* Channel 1: buf[1] → PWM CC, chains to ch0 on completion */
    dma_channel_config c1 = dma_channel_get_default_config(g_dma_ch[1]);
    channel_config_set_transfer_data_size(&c1, DMA_SIZE_32);
    channel_config_set_read_increment(&c1, true);
    channel_config_set_write_increment(&c1, false);
    channel_config_set_dreq(&c1, pwm_get_dreq(slice_num));
    channel_config_set_chain_to(&c1, g_dma_ch[0]);
    dma_channel_configure(g_dma_ch[1], &c1,
        &pwm_hw->slice[slice_num].cc,
        g_audio_buf[1],
        AUDIO_BUF_SAMPLES,
        false);

    dma_channel_set_irq1_enabled(g_dma_ch[0], true);
    dma_channel_set_irq1_enabled(g_dma_ch[1], true);

    
}

void audio_dma_start(void)
{
    for (uint i = 0; i < AUDIO_BUF_SAMPLES; i++)
        g_audio_buf[0][i] = g_audio_buf[1][i] = PWM_MID_STEREO;
    __dmb();
    dma_channel_start(g_dma_ch[0]);
}

void core1_audio_entry(void)
{
    /* Initialize dbopl — float table init runs once here, never inside the ISR.
     * Must complete before DMA starts so the ISR never stalls on OPL3_Reset. */
    OPL3_Reset(&g_chip, 49716);

    /* DMA_IRQ_1 is routed to Core 1's NVIC; must be registered from Core 1. */
    irq_set_exclusive_handler(DMA_IRQ_1, dma_irq1_handler);
    irq_set_enabled(DMA_IRQ_1, true);

    audio_dma_start();

    while (true) {
        /* Between-track reset: Core 0 sets g_playback_active=false then
         * g_reset_opl3=true and waits. ISR uses the silence branch while
         * g_playback_active is false, so g_chip is safe to touch here. */
        if (g_reset_opl3) {
            OPL3_Reset(&g_chip, 49716);
            g_reset_opl3 = false;
        }
        __wfe();
    }
}
