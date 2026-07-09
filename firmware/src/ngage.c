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
  *   bit 7 : LCDM       mode:  1 = pixel-data word, 0 = command word       *
  *   bit 6 : LCDDa6     display data bus, bit 6                            *
  *   bit 5 : LCDDa5     display data bus, bit 5                            *
  *   bit 4 : LCDDa4     display data bus, bit 4                            *
  *   bit 3 : LCDDa3     display data bus, bit 3                            *
  *   bit 2 : LCDDa2     display data bus, bit 2                            *
  *   bit 1 : LCDDa1     display data bus, bit 1                            *
  *   bit 0 : LCDDa0     display data bus, bit 0                            *
  * ----------------------------------------------------------------------- */
#define LCDM             0x80u
#define LCD_PAYLOAD_MASK 0x7Fu  // bits 6-0.
#define LCDLLClk         0x75u  // line-latch clock strobe  (LCDM = 0).
#define LCDDISPClk       0x5Cu  // display pixel-clock strobe (LCDM = 0).

// Nibble decode constants (see decoder.c for full explanation).
#define NIB_DELTA_BIT    3    // LCDDa3 position within a low nibble.
#define NIB_DATA_MASK    0x7  // bits 2-0: colour-data pins per nibble.

// Live RGB565 display buffer - single definition; extern-declared in
// ngage.h.  Zero-initialised; filled continuously by update_display_buffer().
char display_buffer_rgb565[NGAGE_BUFFER_SIZE];

// Persistent decoder state.
static int s_state;
static int s_index;
static int s_target_data[4]; // row-address bytes collected after LCDLLClk.

// Rolling 3-nibble pipeline (two nibbles per bus byte, three per pixel).
static int s_nib[3];
static int s_nib_count;

// Absolute pixel counter within the current DISPClk burst.
static int s_coli;

// Delta predictors for R, G, B MSB carry-over between pixels.
static int s_last_r;
static int s_last_g;
static int s_last_b;

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
 *   n0 (high nibble): [LCDM(1) | LCDDa6 | LCDDa5 | LCDDa4]                *
 *   n1 (low  nibble): [LCDDa3  | LCDDa2 | LCDDa1 | LCDDa0]                *
 *   n2 (high nibble): same layout as n0                                   *
 *                                                                         *
 * LCDDa3 (bit 3 of every low nibble) is a 1-bit delta predictor for the   *
 * channel MSB; high nibbles always carry LCDM=1 so their bit 3 is masked. *
 * ----------------------------------------------------------------------- */
static void process_nibbles(int n0, int n1, int n2)
{
    int r, g, b, cx, cy;

    if (s_coli % 2 == 0)
    {
        s_last_g = n1 >> NIB_DELTA_BIT;
    }
    else
    {
        s_last_r = n0 >> NIB_DELTA_BIT;
        s_last_b = n2 >> NIB_DELTA_BIT;
    }

    r = ((n0 & NIB_DATA_MASK) | (s_last_r << NIB_DELTA_BIT)) * 16;
    g = ((n1 & NIB_DATA_MASK) | (s_last_g << NIB_DELTA_BIT)) * 16;
    b = ((n2 & NIB_DATA_MASK) | (s_last_b << NIB_DELTA_BIT)) * 16;

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

    new_nibs[0] = (b >> 4) & 0xF; // high nibble: [LCDM | LCDDa6 | LCDDa5 | LCDDa4]
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
 * decoder.c.  Command words (LCDM=0) drive state transitions; pixel-data *
 * words (LCDM=1) are forwarded to the nibble decoder.                    *
 * ---------------------------------------------------------------------- */
static void process_byte(uint8_t b)
{
    if ((b & LCDM) == 0)
    {
        // Command word.
        uint8_t strobe = b & LCD_PAYLOAD_MASK;

        if (strobe == LCDLLClk)
        {
            // Line-latch clock: start of a new frame segment.
            // Reset nibble pipeline, pixel counter, and delta predictors.
            s_state = LCDLLClk;
            s_index = 0;
            s_nib_count = 0;
            s_coli = 0;
            s_last_r = 0;
            s_last_g = 0;
            s_last_b = 0;
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
}

void update_display_buffer(void)
{
    /* Drain every byte the PIO has captured since the last call.         */
    while (!pio_sm_is_rx_fifo_empty(NGAGE_LCD_PIO, NGAGE_LCD_SM))
    {
        uint32_t raw = pio_sm_get(NGAGE_LCD_PIO, NGAGE_LCD_SM);
        process_byte((uint8_t)(raw & 0xFF));
    }
}
