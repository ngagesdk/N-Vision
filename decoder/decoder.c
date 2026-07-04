/** @file decoder.c
 *
 *  A proof of concept for decoding the Nokia N-Gage LCD framebuffer
 *  from a raw binary dump.
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

/* -----------------------------------------------------------------------
 * LCD bus signals - every bit position carries the name of its physical pin.
 *
 * Raw stream byte layout (MSB first):
 *   bit 7 : LCDM       – mode:  1 = pixel-data word, 0 = command word
 *   bit 6 : LCDFSP     – frame start pulse       (pixel-data words only)
 *   bit 5 : LCDDe5     – display data bus, bit 5 (pixel-data words only)
 *   bit 4 : LCDDe4     – display data bus, bit 4
 *   bit 3 : LCDDe3     – display data bus, bit 3
 *   bit 2 : LCDDe2     – display data bus, bit 2
 *   bit 1 : LCDDe1     – display data bus, bit 1
 *   bit 0 : LCDDe0     – display data bus, bit 0
 *
 * Command words (LCDM = 0): bits 6-0 identify the control strobe asserted:
 *   LCDLLClk   0x75 – line-latch clock  (followed by target-row data words)
 *   LCDDISPClk 0x5C – display pixel clock (followed by pixel data words)
 * ----------------------------------------------------------------------- */
#define LCDM   0x80u /* bit 7 */
#define LCDFSP 0x40u /* bit 6 */
#define LCDDe5 0x20u /* bit 5 */
#define LCDDe4 0x10u /* bit 4 */
#define LCDDe3 0x08u /* bit 3 */
#define LCDDe2 0x04u /* bit 2 */
#define LCDDe1 0x02u /* bit 1 */
#define LCDDe0 0x01u /* bit 0 */

#define LCDLLClk   0x75u /* line-latch clock strobe  (LCDM = 0) */
#define LCDDISPClk 0x5Cu /* display pixel-clock strobe (LCDM = 0) */

/* Composite masks used in nibble decode */
#define LCD_PAYLOAD_MASK (LCDFSP | LCDDe5 | LCDDe4 | LCDDe3 | LCDDe2 | LCDDe1 | LCDDe0) /* bits 6-0 */
#define NIB_DELTA_BIT    3                                                              /* LCDDe3 position within a low nibble */
#define NIB_DATA_MASK    0x7                                                            /* bits 2-0: color-data pins per nibble  */

static uint8_t framebuf[HEIGHT * WIDTH * 3];

/*
 * Decode nibble-encoded pixel data and write it into framebuf.
 *
 * Each data word (LCDM = 1) expands to two nibbles:
 *   high nibble (bits 7-4): [LCDM(1) | LCDFSP | LCDDe5 | LCDDe4]
 *   low  nibble (bits 3-0): [LCDDe3  | LCDDe2 | LCDDe1 | LCDDe0]
 *
 * Three consecutive nibbles encode one RGB pixel (n0 = R, n1 = G, n2 = B).
 * LCDDe3 (NIB_DELTA_BIT of every low nibble) is a 1-bit delta predictor for
 * the channel MSB.  Nibble alignment ensures the delta nibble is always a
 * low nibble:
 *   even pixel: n1 is low -> LCDDe3 updates last_g
 *   odd  pixel: n0, n2 are low -> LCDDe3 updates last_r / last_b
 *
 * Channel value = (NIB_DATA_MASK bits | last_x << NIB_DELTA_BIT) * 16  -> 0-240
 */
static void decode_and_draw(const uint8_t *imdata, size_t imdata_len, int y_offset)
{
    size_t nibble_count = imdata_len * 2;
    uint8_t *nib = (uint8_t *)malloc(nibble_count);
    if (!nib)
        return;

    for (size_t k = 0; k < imdata_len; k++)
    {
        nib[k * 2 + 0] = (imdata[k] >> 4) & 0xf; /* high: [LCDM | LCDFSP | LCDDe5 | LCDDe4] */
        nib[k * 2 + 1] = imdata[k] & 0xf;        /* low:  [LCDDe3 | LCDDe2 | LCDDe1 | LCDDe0] */
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
            last_g = n1 >> NIB_DELTA_BIT; /* LCDDe3: G-channel MSB predictor */
        }
        else
        {
            last_r = n0 >> NIB_DELTA_BIT; /* LCDDe3: R-channel MSB predictor */
            last_b = n2 >> NIB_DELTA_BIT; /* LCDDe3: B-channel MSB predictor */
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

int main(void)
{
    const char *input_file = "fullboot_with_sim.bin";
    FILE *fp = fopen(input_file, "rb");
    if (!fp)
    {
        fprintf(stderr, "Cannot open %s\n", input_file);
        return 1;
    }

    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    rewind(fp);

    uint8_t *data = (uint8_t *)malloc((size_t)fsize);
    if (!data)
    {
        fclose(fp);
        return 1;
    }
    fread(data, 1, (size_t)fsize, fp);
    fclose(fp);

    _mkdir("frames");
    memset(framebuf, 0, sizeof(framebuf));

    int state = 0;
    int target_data[4] = { 0, 0, 0, 0 };
    int index = 0;
    uint8_t *imdata = NULL;
    size_t imdata_len = 0;
    size_t imdata_cap = 0;
    int imageindex = 0;

    for (long i = 0; i < fsize; i++)
    {
        uint8_t b = data[i];

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
                    target_data[index] = b & LCD_PAYLOAD_MASK; /* [LCDFSP | LCDDe5:LCDDe0] */
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
                        free(data);
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
    free(data);
    return 0;
}
