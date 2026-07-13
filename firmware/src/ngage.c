/** @file ngage.c
 *
 *  PIO-based N-Gage LCD bus decoder.
 *
 *  The PIO state machine (ngage_lcd.pio) captures every 8-bit bus word on
 *  each rising clock edge and pushes it to the RX FIFO.  update_display_buffer()
 *  drains that FIFO on every frame and feeds the bytes through the same
 *  nibble-level decode algorithm used by decoder.c, writing the resulting
 *  RGB565 pixels directly into display_buffer_rgb565.
 *
 *  Copyright (c) 2026, Michael Fitzmayer. All rights reserved.
 *  SPDX-License-Identifier: MIT
 *
 **/

#include <string.h>
#include "hardware/pio.h"
#include "ngage.h"

 /* ----------------------------------------------------------------------- *
  * LCD bus signal definitions                                              *
  *                                                                         *
  * Raw bus byte layout (MSB first - matches PIO capture with in_base wired *
  * MSB-first as documented in ngage_lcd.h):                                *
  *   bit 7 : LCDDa7     display data bus, bit 7                            *
  *   bit 6 : LCDDa6     display data bus, bit 6                            *
  *   bit 5 : LCDDa5     display data bus, bit 5                            *
  *   bit 4 : LCDDa4     display data bus, bit 4                            *
  *   bit 3 : LCDDa3     display data bus, bit 3                            *
  *   bit 2 : LCDDa2     display data bus, bit 2                            *
  *   bit 1 : LCDDa1     display data bus, bit 1                            *
  *   bit 0 : LCDDa0     display data bus, bit 0                            *
  *                                                                         *
  * LCDM (mode flag: 1=pixel-data, 0=command) is a separate GPIO signal,    *
  * NOT included in the captured data byte. Read via gpio_get() separately. *
  * ----------------------------------------------------------------------- */
#define LCDM_HIGH        0x01u  // LCDM mode flag value (macro for clarity)
#define LCD_PAYLOAD_MASK 0xFFu  // all bits 7-0: data bus (LCDDa7-LCDDa0).
#define LCDLLClk         0x75u  // line-latch clock strobe  (LCDM = 0).
#define LCDDISPClk       0x5Cu  // display pixel-clock strobe (LCDM = 0).

// Nibble scale factor: each 4-bit nibble maps to an 8-bit channel value.
#define NIB_SCALE        16   // nibble * 16 gives the 8-bit channel value.

// Live RGB565 display buffer - single definition; extern-declared in
// ngage.h.  Zero-initialised; filled continuously by update_display_buffer().
char display_buffer_rgb565[NGAGE_BUFFER_SIZE];

// Persistent decoder state.
static int s_state;
static int s_index;
static int s_target_data[4]; // row-address bytes collected after LCDLLClk.
static int s_lcdm;           // LCDM mode signal (1=pixel-data, 0=command)

// Rolling 3-nibble pipeline (two nibbles per bus byte, three per pixel).
static int s_nib[3];
static int s_nib_count;

// Absolute pixel counter within the current DISPClk burst.
static int s_coli;

/* ---------------------------------------------------------------------- *
 * write_pixel                                                            *
 *                                                                        *
 * Converts raw channel values (0–240) to little-endian RGB565 and writes *
 * them into display_buffer_rgb565.                                       *
 * ---------------------------------------------------------------------- */
static void write_pixel(int x, int y, int r, int g, int b)
{
    uint16_t pixel;
    int      idx;

    if (x < 0 || x >= NGAGE_DISPLAY_WIDTH ||
        y < 0 || y >= NGAGE_DISPLAY_HEIGHT)
    {
        return;
    }

    pixel = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    idx = (y * NGAGE_BUFFER_WIDTH) + (x * 2);

    display_buffer_rgb565[idx] = (char)(pixel & 0xFF);
    display_buffer_rgb565[idx + 1] = (char)(pixel >> 8);
}

/* ----------------------------------------------------------------------- *
 * process_nibbles                                                         *
 *                                                                         *
 * Decodes one RGB pixel from three consecutive nibbles:                   *
 *   n0 (high nibble): [LCDDa7 | LCDDa6 | LCDDa5 | LCDDa4]                 *
 *   n1 (low  nibble): [LCDDa3 | LCDDa2 | LCDDa1 | LCDDa0]                 *
 *   n2 (high nibble): same layout as n0                                   *
 *                                                                         *
 * All 4 bits of each nibble are data; bit 3 is NOT a delta predictor.     *
 * Each nibble value (0-15) is scaled directly to an 8-bit channel value.  *
 * ----------------------------------------------------------------------- */
static void process_nibbles(int n0, int n1, int n2)
{
    int r, g, b, cx, cy;

    r = n0 * NIB_SCALE;
    g = n1 * NIB_SCALE;
    b = n2 * NIB_SCALE;

    cx = s_coli % NGAGE_DISPLAY_WIDTH;
    cy = s_target_data[0] + (s_coli / NGAGE_DISPLAY_WIDTH);

    write_pixel(cx, cy, r, g, b);
    s_coli++;
}

/* ---------------------------------------------------------------------- *
 * process_pixel_byte                                                     *
 *                                                                        *
 * Splits one pixel-data bus byte into its high and low nibbles and feeds *
 * the rolling 3-nibble decoder.  Every three nibbles yield one pixel.    *
 * ---------------------------------------------------------------------- */
static void process_pixel_byte(uint8_t b)
{
    int i;
    int new_nibs[2];

    new_nibs[0] = (b >> 4) & 0xF; // high nibble: [LCDDa7 | LCDDa6 | LCDDa5 | LCDDa4]
    new_nibs[1] = b & 0xF;        // low  nibble: [LCDDa3 | LCDDa2 | LCDDa1 | LCDDa0]

    for (i = 0; i < 2; i++)
    {
        s_nib[s_nib_count++] = new_nibs[i];
        if (s_nib_count == 3)
        {
            process_nibbles(s_nib[0], s_nib[1], s_nib[2]);
            s_nib_count = 0;
        }
    }
}

/* ---------------------------------------------------------------------- *
 * process_byte                                                           *
 *                                                                        *
 * Top-level bus-word state machine; mirrors the main parse loop in       *
 * decoder_csv.c. The LCDM mode flag (read separately) determines if      *
 * this data byte is a command word (LCDM=0) or pixel-data (LCDM=1).      *
 * ---------------------------------------------------------------------- */
static void process_byte(uint8_t b)
{
    if (s_lcdm == 0)
    {
        // Command word (LCDM = 0).
        uint8_t strobe = b & LCD_PAYLOAD_MASK;

        if (strobe == LCDLLClk)
        {
            // Line-latch clock: start of a new frame segment.
            // Reset nibble pipeline, pixel counter, and delta predictors.
            s_state = LCDLLClk;
            s_index = 0;
            s_nib_count = 0;
            s_coli = 0;
        }
        else if (strobe == LCDDISPClk)
        {
            // Display pixel clock: pixel-data bytes follow.
            s_state = LCDDISPClk;
            s_index = 0;
        }
        else
        {
            s_state = 0;
            s_index = 0;
        }
    }
    else
    {
        // Pixel-data word (LCDM = 1).
        if (s_state == LCDLLClk)
        {
            // Collect up to 4 row-address bytes; [0] is used as y_offset.
            if (s_index < 4)
            {
                s_target_data[s_index] = (int)(b & LCD_PAYLOAD_MASK);
            }
            s_index++;
        }
        else if (s_state == LCDDISPClk)
        {
            process_pixel_byte(b);
            s_index++;
        }
    }
}

void ngage_lcd_init(void)
{
    uint offset = pio_add_program(NGAGE_LCD_PIO, &ngage_lcd_program);
    ngage_lcd_program_init(NGAGE_LCD_PIO, NGAGE_LCD_SM, offset,
        NGAGE_LCD_DATA_BASE_PIN, NGAGE_LCD_CLK_PIN,
        1.0f);

    // Initialize LCDM GPIO as input
    gpio_init(NGAGE_LCD_LCDM_PIN);
    gpio_set_dir(NGAGE_LCD_LCDM_PIN, GPIO_IN);
}

void update_display_buffer(void)
{
    /* Drain every byte the PIO has captured since the last call.         */
    while (!pio_sm_is_rx_fifo_empty(NGAGE_LCD_PIO, NGAGE_LCD_SM))
    {
        uint32_t raw = pio_sm_get(NGAGE_LCD_PIO, NGAGE_LCD_SM);

        // Read LCDM GPIO state (mode flag: 1=pixel-data, 0=command)
        s_lcdm = gpio_get(NGAGE_LCD_LCDM_PIN) ? 1 : 0;

        process_byte((uint8_t)(raw & 0xFF));
    }
}
