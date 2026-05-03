#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/stdio_usb.h"
#include "hardware/clocks.h"

#include "shared.h"
#include "audio_pwm.h"
#include "vgm.h"
#include "usb_transfer.h"

/* 400 KB — fits the vast majority of OPL3 VGM files in RP2350's 520 KB SRAM. */
#define VGM_BUF_SIZE (400u * 1024u)

/* Globals declared in shared.h */
cmd_queue_t       g_cmd_queue;
volatile uint32_t g_opl3_samples_generated;
volatile uint32_t g_opl3_sample_base;
volatile bool     g_playback_active;
volatile bool     g_reset_opl3;

static uint8_t g_vgm_buf[VGM_BUF_SIZE] __attribute__((aligned(4)));

int main(void)
{
    /* RP2350 defaults to 150 MHz; pin it explicitly so PWM constants match. */
    set_sys_clock_khz(150000, true);

    stdio_init_all();

    /* Drive GPIO 23 high to force the SMPS into fixed-frequency PWM mode,
     * reducing switching-noise coupling into the audio output. */
    gpio_init(23);
    gpio_set_dir(23, GPIO_OUT);
    gpio_put(23, 1);

    /* Initialise stereo PWM on GPIO16/17 and claim DMA channels before Core 1 starts. */
    audio_pwm_init();

    /* Launch Core 1; it initialises dbopl (slow table init), registers the
     * DMA ISR on its own NVIC, and starts the audio pipeline. */
    multicore_launch_core1(core1_audio_entry);

    /* Wait for USB host enumeration. */
    while (!stdio_usb_connected())
        sleep_ms(10);

    while (true) {
        uint32_t vgm_len = 0;

        if (!usb_receive_vgm(g_vgm_buf, VGM_BUF_SIZE, &vgm_len)) {
            printf("Transfer error\n");
            stdio_flush();
            continue;
        }

        vgm_state_t vgm;
        if (!vgm_open(&vgm, g_vgm_buf, vgm_len)) {
            printf("Bad VGM header\n");
            stdio_flush();
            continue;
        }

        /* Stop playback, then ask Core 1 to reset the chip.
         * g_playback_active must be false before g_reset_opl3 is set so the
         * ISR uses the silence branch and never touches g_chip during reset. */
        g_playback_active = false;
        __dmb();
        g_cmd_queue.head  = 0;
        g_cmd_queue.tail  = 0;
        g_reset_opl3      = true;
        /* Spin until Core 1 main loop clears the flag (fast: tables cached). */
        while (g_reset_opl3)
            tight_loop_contents();
        __dmb();
        g_opl3_sample_base = g_opl3_samples_generated;
        __dmb();
        g_playback_active  = true;

        while (!vgm.finished)
            vgm_step(&vgm);

        printf("DONE\n");
        stdio_flush();
    }
}
