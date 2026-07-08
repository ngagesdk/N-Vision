/** @file decoder_csv.c
 *
 *  CSV variant of the Nokia N-Gage LCD framebuffer decoder.
 *
 *  Reads a libsigrok4DSL CSV export (16 channels) instead of a raw
 *  binary dump and produces the same per-frame PNG files as
 *  decoder.c.
 *
 *  *** CONFIGURATION ***
 *  Edit the CH_xxx defines in the "Channel mapping" section below to match
 *  which CSV column index (0-15) is connected to each LCD bus signal.
 *
 *  Expected CSV format (libsigrok4DSL):
 *    Lines beginning with ';' are comments and are ignored.
 *    The first non-comment line is the column header and is ignored.
 *    Every data line: Time(s),ch0,ch1,...,ch15
 *    Channel values are 0 or 1; the timestamp column is ignored.
 *
 *  Copyright (c) 2026, Michael Fitzmayer. All rights reserved.
 *  SPDX-License-Identifier: MIT
 *
 **/

#include <direct.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define WIDTH        176
#define HEIGHT       208
#define TOTAL_PIXELS (WIDTH * HEIGHT)

/* =======================================================================
 * Channel mapping — edit these to match your logic analyser probe wiring.
 *
 * Each CH_xxx is the zero-based CSV column index (0-15) connected to the
 * named LCD bus signal.
 * ====================================================================== */
#define CH_LCDCLK 9  /* CSV column wired to LCDCLK                        */
#define CH_LATCH  1  /* CSV column wired to LATCH                         */
#define CH_LCDM   11 /* CSV column wired to LCDM   (bus bit 7, mode flag) */
#define CH_LCDDa6 12 /* CSV column wired to LCDDa6 (bus bit 6)            */
#define CH_LCDDa5 13 /* CSV column wired to LCDDa5 (bus bit 5)            */
#define CH_LCDDa4 6  /* CSV column wired to LCDDa4 (bus bit 4)            */
#define CH_LCDDa3 5  /* CSV column wired to LCDDa3 (bus bit 3)            */
#define CH_LCDDa2 4  /* CSV column wired to LCDDa2 (bus bit 2)            */
#define CH_LCDDa1 3  /* CSV column wired to LCDDa1 (bus bit 1)            */
#define CH_LCDDa0 2  /* CSV column wired to LCDDa0 (bus bit 0)            */

// 11, 12, 13, 6, 5, 4, 3, 2 !

/* -----------------------------------------------------------------------
 * LCD bus signals - every bit position carries the name of its physical pin.
 * Raw stream byte layout (MSB first):
 *
 *   bit 7 : LCDM       – mode: 1 = pixel-data word, 0 = command word
 *   bit 6 : LCDDa6     – display data bus, bit 6
 *   bit 5 : LCDDa5     – display data bus, bit 5
 *   bit 4 : LCDDa4     – display data bus, bit 4
 *   bit 3 : LCDDa3     – display data bus, bit 3
 *   bit 2 : LCDDa2     – display data bus, bit 2
 *   bit 1 : LCDDa1     – display data bus, bit 1
 *   bit 0 : LCDDa0     – display data bus, bit 0
 *
 * Command words (LCDM = 0): bits 6-0 identify the control strobe asserted:
 *   LCDLLClk   0x75 – line-latch clock  (followed by target-row data words)
 *   LCDDISPClk 0x5C – display pixel clock (followed by pixel data words)
 * ----------------------------------------------------------------------- */
#define LCDM   0x80u /* bit 7 */
#define LCDDa6 0x40u /* bit 6 */
#define LCDDa5 0x20u /* bit 5 */
#define LCDDa4 0x10u /* bit 4 */
#define LCDDa3 0x08u /* bit 3 */
#define LCDDa2 0x04u /* bit 2 */
#define LCDDa1 0x02u /* bit 1 */
#define LCDDa0 0x01u /* bit 0 */

#define LCDLLClk   0x75u /* line-latch clock strobe  (LCDM = 0) */
#define LCDDISPClk 0x5Cu /* display pixel-clock strobe (LCDM = 0) */

/* Composite masks used in nibble decode */
#define LCD_PAYLOAD_MASK (LCDDa6 | LCDDa5 | LCDDa4 | LCDDa3 | LCDDa2 | LCDDa1 | LCDDa0) /* bits 6-0 */
#define NIB_DELTA_BIT    3                                                              /* LCDDa3 position within a low nibble */
#define NIB_DATA_MASK    0x7                                                            /* bits 2-0: color-data pins per nibble  */

#define MAX_CHANNELS 16
#define CSV_LINE_MAX 512

static uint8_t framebuf[HEIGHT * WIDTH * 3];

/*
 * Decode nibble-encoded pixel data and write it into framebuf.
 * Identical to the implementation in decoder.c.
 */
static void decode_and_draw(const uint8_t *imdata, size_t imdata_len, int y_offset)
{
    size_t nibble_count = imdata_len * 2;
    uint8_t *nib = (uint8_t *)malloc(nibble_count);
    if (!nib)
        return;

    for (size_t k = 0; k < imdata_len; k++)
    {
        nib[k * 2 + 0] = (imdata[k] >> 4) & 0xf; /* high: [LCDM | LCDDa6 | LCDDa5 | LCDDa4] */
        nib[k * 2 + 1] = imdata[k] & 0xf;        /* low:  [LCDDa3 | LCDDa2 | LCDDa1 | LCDDa0] */
    }

    int last_r = 0, last_g = 0, last_b = 0;
    int coli = 0;
    size_t off = 0;

    while (nibble_count - off >= 3)
    {
        int n0 = nib[off + 0];
        int n1 = nib[off + 1];
        int n2 = nib[off + 2];

        if (coli % 2 == 0)
        {
            last_g = n1 >> NIB_DELTA_BIT; /* LCDDa3: G-channel MSB predictor */
        }
        else
        {
            last_r = n0 >> NIB_DELTA_BIT; /* LCDDa3: R-channel MSB predictor */
            last_b = n2 >> NIB_DELTA_BIT; /* LCDDa3: B-channel MSB predictor */
        }

        int r = ((n0 & NIB_DATA_MASK) | (last_r << NIB_DELTA_BIT)) * 16;
        int g = ((n1 & NIB_DATA_MASK) | (last_g << NIB_DELTA_BIT)) * 16;
        int b = ((n2 & NIB_DATA_MASK) | (last_b << NIB_DELTA_BIT)) * 16;

        int cx = coli % WIDTH;
        int cy = y_offset + (coli / WIDTH);

        if (cx >= 0 && cx < WIDTH && cy >= 0 && cy < HEIGHT)
        {
            size_t idx = (size_t)((cy * WIDTH + cx) * 3);
            framebuf[idx + 0] = (uint8_t)r;
            framebuf[idx + 1] = (uint8_t)g;
            framebuf[idx + 2] = (uint8_t)b;
        }

        off += 3;
        coli += 1;
    }

    free(nib);
}

/*
 * Reconstruct a single bus byte from one CSV row's sampled channel values.
 * ch[i] is 1 if channel i is high, 0 if low.
 */
static uint8_t channels_to_bus_byte(const int ch[MAX_CHANNELS])
{
    uint8_t b = 0;
    if (ch[CH_LCDM])
    {
        b |= LCDM;
    }
    if (ch[CH_LCDDa6])
    {
        b |= LCDDa6;
    }
    if (ch[CH_LCDDa5])
    {
        b |= LCDDa5;
    }
    if (ch[CH_LCDDa4])
    {
        b |= LCDDa4;
    }
    if (ch[CH_LCDDa3])
    {
        b |= LCDDa3;
    }
    if (ch[CH_LCDDa2])
    {
        b |= LCDDa2;
    }
    if (ch[CH_LCDDa1])
    {
        b |= LCDDa1;
    }
    if (ch[CH_LCDDa0])
    {
        b |= LCDDa0;
    }
    return b;
}

/*
 * Parse one CSV data line into ch[0..MAX_CHANNELS-1].
 * Modifies 'line' in place (strtok).
 *
 * Skips:
 *   - Comment lines  (first char ';')
 *   - The header line (first char 'T' for "Time(s)")
 *   - Blank lines
 *
 * Returns the number of channel values parsed, or -1 to skip the line.
 */
static int parse_csv_row(char *line, int ch[MAX_CHANNELS])
{
    if (line[0] == ';' || line[0] == 'T' ||
        line[0] == '\n' || line[0] == '\r' || line[0] == '\0')
        return -1;

    char *tok = strtok(line, ","); /* consume the timestamp field */
    if (!tok)
        return -1;

    int count = 0;
    while (count < MAX_CHANNELS && (tok = strtok(NULL, ", \t\r\n")) != NULL)
        ch[count++] = atoi(tok);

    return count;
}

/*
 * Verify at runtime that all CH_xxx indices fall within [0, MAX_CHANNELS).
 * Returns 1 on success, 0 on failure.
 */
static int validate_channel_map(void)
{
    static const int map[] = {
        CH_LCDCLK, CH_LCDM, CH_LCDDa6,
        CH_LCDDa5, CH_LCDDa4, CH_LCDDa3, CH_LCDDa2, CH_LCDDa1, CH_LCDDa0
    };
    size_t i;
    for (i = 0; i < sizeof(map) / sizeof(map[0]); i++)
    {
        if (map[i] < 0 || map[i] >= MAX_CHANNELS)
        {
            fprintf(stderr, "Channel map error: index %d is out of range [0, %d)\n",
                    map[i], MAX_CHANNELS);
            return 0;
        }
    }
    return 1;
}

int main(void)
{
    if (!validate_channel_map())
    {
        return 1;
    }

    const char *input_file = "trace.csv";
    FILE *fp = fopen(input_file, "r");
    if (!fp)
    {
        fprintf(stderr, "Cannot open %s\n", input_file);
        return 1;
    }

    _mkdir("frames");
    memset(framebuf, 0, sizeof(framebuf));

    int state = 0;
    int target_data[4] = { 0, 0, 0, 0 };
    int index = 0;
    uint8_t *imdata = NULL;
    size_t imdata_len = 0;
    size_t imdata_cap = 0;
    int imageindex = 0;

    char line[CSV_LINE_MAX];
    int ch[MAX_CHANNELS];
    int prev_clk = 0;

    while (fgets(line, sizeof(line), fp))
    {
        memset(ch, 0, sizeof(ch));
        if (parse_csv_row(line, ch) <= 0)
        {
            continue;
        }

        /* Sample the bus only on the rising edge of LCDCLK */
        int clk = ch[CH_LCDCLK];
        int rising = (prev_clk == 0 && clk == 1);
        prev_clk = clk;
        if (!rising)
        {
            continue;
        }

        uint8_t b = channels_to_bus_byte(ch);

        if ((b & LCDM) == 0)
        {
            /* Command word: LCDM = 0, flush any pending pixel data first */
            if (index > 0)
            {
                decode_and_draw(imdata, imdata_len, target_data[0]);
                imageindex++;
                char path[64];
                snprintf(path, sizeof(path), "frames/%04d.png", imageindex);
                stbi_write_png(path, WIDTH, HEIGHT, 3, framebuf, WIDTH * 3);
            }

            uint8_t strobe = b & LCD_PAYLOAD_MASK;
            if (strobe == LCDLLClk)
            {
                state = LCDLLClk;
                imdata_len = 0; /* start fresh pixel buffer for this frame */
                index = 0;
            }
            else if (strobe == LCDDISPClk)
            {
                state = LCDDISPClk;
                index = 0; /* imdata intentionally NOT reset - accumulates */
            }
            else
            {
                state = 0;
                index = 0;
            }
        }
        else
        {
            /* Pixel-data word: LCDM = 1 */
            if (state == LCDLLClk)
            {
                if (index < 4)
                {
                    target_data[index] = b & LCD_PAYLOAD_MASK; /* [LCDDa6 | LCDDa5:LCDDa0] */
                }
                index++;
            }
            else if (state == LCDDISPClk)
            {
                if (imdata_len >= imdata_cap)
                {
                    size_t new_cap = (imdata_cap == 0) ? 4096 : imdata_cap * 2;
                    uint8_t *tmp = (uint8_t *)realloc(imdata, new_cap);
                    if (!tmp)
                    {
                        free(imdata);
                        fclose(fp);
                        return 1;
                    }
                    imdata = tmp;
                    imdata_cap = new_cap;
                }
                imdata[imdata_len++] = b;
                index++;
            }
        }
    }

    /* Flush any trailing draw data at end of file */
    if (index > 0 && imdata_len > 0)
    {
        decode_and_draw(imdata, imdata_len, target_data[0]);
        imageindex++;
        char path[64];
        snprintf(path, sizeof(path), "frames/%04d.png", imageindex);
        stbi_write_png(path, WIDTH, HEIGHT, 3, framebuf, WIDTH * 3);
    }

    printf("Saved %d frames to frames/.\n", imageindex);

    free(imdata);
    fclose(fp);
    return 0;
}
