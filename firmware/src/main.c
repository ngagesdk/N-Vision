/** @file main.c
 *
 *  A PIO-based N-Gage LCD bus decoder.
 *
 *  Copyright (c) 2026, Michael Fitzmayer. All rights reserved.
 *  SPDX-License-Identifier: MIT
 *
 **/

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/gpio.h"
#include "hardware/vreg.h"

#include "dvi.h"
#include "dvi_serialiser.h"
#include "common_dvi_pin_configs.h"

#include "frame_buffer_rgb565.h"
#include "ngage.h"

// DVDD 1.2V (1.1V seems ok too)
#define VREG_VSEL  VREG_VOLTAGE_1_20
#define DVI_TIMING dvi_timing_640x480p_60hz

struct dvi_inst dvi0;

static uint16_t s_overlay_scanline_buffer[FRAME_WIDTH];

void core1_main()
{
    dvi_register_irqs_this_core(&dvi0, DMA_IRQ_0);
    while (queue_is_empty(&dvi0.q_colour_valid))
    {
        __wfe();
    }
    dvi_start(&dvi0);
    dvi_scanbuf_main_16bpp(&dvi0);
}

int main()
{
    const uint8_t *base_frame_bytes = (const uint8_t *)frame_buffer_rgb565;

    vreg_set_voltage(VREG_VSEL);
    sleep_ms(10);
    set_sys_clock_khz(DVI_TIMING.bit_clk_khz, true);

    setup_default_uart();

    dvi0.timing  = &DVI_TIMING;
    dvi0.ser_cfg = DVI_DEFAULT_SERIAL_CONFIG;
    dvi_init(&dvi0, next_striped_spin_lock_num(), next_striped_spin_lock_num());

    // Core 1 will wait until it sees the first colour buffer, then
    // start up the DVI signalling.
    multicore_launch_core1(core1_main);

    // Initialise the PIO-based N-Gage LCD bus decoder.
    ngage_lcd_init();

    // Pass out pointers into our preprepared image, discard the
    // pointers when returned to us.
    while(true)
    {
        update_display_buffer();

        for (uint curr_line = 0; curr_line < FRAME_HEIGHT; ++curr_line)
        {
            uint16_t *scanline;

            if (curr_line >= DISPLAY_OFFSET_Y &&
                curr_line < (DISPLAY_OFFSET_Y + NGAGE_DISPLAY_HEIGHT))
            {
                uint disp_line = curr_line - DISPLAY_OFFSET_Y;
                uint32_t free_scanline_u32;

                scanline = s_overlay_scanline_buffer;

                memcpy(scanline,
                       &base_frame_bytes[curr_line * FRAME_WIDTH * 2],
                       FRAME_WIDTH * 2);

                memcpy(((uint8_t *)scanline) + (DISPLAY_OFFSET_X * 2),
                       &display_buffer_rgb565[disp_line * NGAGE_BUFFER_WIDTH],
                       NGAGE_BUFFER_WIDTH);

                // Reuse a single overlay scanline buffer safely by waiting
                // until DVI has consumed it before rendering the next line.
                queue_add_blocking_u32(&dvi0.q_colour_valid, &scanline);
                do
                {
                    queue_remove_blocking_u32(&dvi0.q_colour_free, &free_scanline_u32);
                }
                while ((uint16_t *)free_scanline_u32 != scanline);

                continue;
            }
            else
            {
                scanline = (uint16_t *)&((const uint16_t *)frame_buffer_rgb565)[curr_line * FRAME_WIDTH];
            }

            queue_add_blocking_u32(&dvi0.q_colour_valid, &scanline);
        }
    }
}
