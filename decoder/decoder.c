#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <direct.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define WIDTH        176
#define HEIGHT       208
#define TOTAL_PIXELS (WIDTH * HEIGHT)

#define CMD_TARGET 0x75
#define CMD_DRAW   0x5c

/* Persistent framebuffer — never cleared, matching Python's cumulative image object */
static uint8_t framebuf[HEIGHT * WIDTH * 3];

/*
 * Decode nibble-encoded pixel data and write it into framebuf.
 *
 * Format: each raw byte in imdata has its MSB set (0x80-0xFF).
 * Each byte expands to two 4-bit nibbles (high then low).
 * Three consecutive nibbles encode one RGB pixel with 1-bit delta encoding:
 *   even pixel:  bit3 of nibble[1] updates last_g
 *   odd  pixel:  bit3 of nibble[0] updates last_r, bit3 of nibble[2] updates last_b
 * Channel value = (nibble & 0x7 | last_x << 3) * 16  -> 0-240
 *
 * Coordinate fix (rotation):
 *   The Python version outputs a 208×176 landscape image (fast axis = display column,
 *   slow axis = display row).  Swapping the axes here produces the correct 176×208
 *   portrait image: cx = coli % WIDTH, cy = y_offset + coli / WIDTH.
 */
static void decode_and_draw(const uint8_t *imdata, size_t imdata_len, int y_offset)
{
    size_t  nibble_count = imdata_len * 2;
    uint8_t *nib = (uint8_t *)malloc(nibble_count);
    if (!nib)
        return;

    for (size_t k = 0; k < imdata_len; k++) {
        nib[k * 2 + 0] = (imdata[k] >> 4) & 0xf;
        nib[k * 2 + 1] =  imdata[k]        & 0xf;
    }

    int    last_r = 0, last_g = 0, last_b = 0;
    int    coli   = 0;
    size_t off    = 0;

    while (nibble_count - off >= 3) {
        int n0 = nib[off + 0];
        int n1 = nib[off + 1];
        int n2 = nib[off + 2];

        if (coli % 2 == 0)
            last_g = n1 >> 3;
        else {
            last_r = n0 >> 3;
            last_b = n2 >> 3;
        }

        int r = ((n0 & 0x7) | (last_r << 3)) * 16;
        int g = ((n1 & 0x7) | (last_g << 3)) * 16;
        int b = ((n2 & 0x7) | (last_b << 3)) * 16;

        /* Fix B (rotation): swap fast/slow axes → portrait 176×208 */
        int cx = coli % WIDTH;
        int cy = y_offset + (coli / WIDTH);

        if (cx >= 0 && cx < WIDTH && cy >= 0 && cy < HEIGHT) {
            size_t idx        = (size_t)((cy * WIDTH + cx) * 3);
            framebuf[idx + 0] = (uint8_t)r;
            framebuf[idx + 1] = (uint8_t)g;
            framebuf[idx + 2] = (uint8_t)b;
        }

        off  += 3;
        coli += 1;
    }

    free(nib);
}

int main(void)
{
    const char *input_file = "fullboot_with_sim.bin";
    FILE *fp = fopen(input_file, "rb");
    if (!fp) {
        fprintf(stderr, "Cannot open %s\n", input_file);
        return 1;
    }

    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    rewind(fp);

    uint8_t *data = (uint8_t *)malloc((size_t)fsize);
    if (!data) {
        fclose(fp);
        return 1;
    }
    fread(data, 1, (size_t)fsize, fp);
    fclose(fp);

    _mkdir("imgs");
    memset(framebuf, 0, sizeof(framebuf));

    int     state          = 0;
    int     target_data[4] = {0, 0, 0, 0};
    int     index          = 0;
    uint8_t *imdata        = NULL;
    size_t  imdata_len     = 0;
    size_t  imdata_cap     = 0;
    int     imageindex     = 0;

    for (long i = 0; i < fsize; i++) {
        uint8_t b = data[i];

        if ((b & 0x80) == 0) {
            /* Command byte: flush any pending data accumulated since last command */
            if (index > 0) {
                decode_and_draw(imdata, imdata_len, target_data[0]);
                imageindex++;
                char path[64];
                snprintf(path, sizeof(path), "imgs/%04d.png", imageindex);
                stbi_write_png(path, WIDTH, HEIGHT, 3, framebuf, WIDTH * 3);
            }

            int command = b & 0x7f;
            if (command == CMD_TARGET) {
                state      = CMD_TARGET;
                imdata_len = 0;   /* start fresh pixel buffer for this frame */
                index      = 0;
            } else if (command == CMD_DRAW) {
                state = CMD_DRAW;
                index = 0;        /* imdata intentionally NOT reset — accumulates */
            } else {
                state = 0;
                index = 0;
            }
        } else {
            if (state == CMD_TARGET) {
                if (index < 4)
                    target_data[index] = b & 0x7f;
                index++;
            } else if (state == CMD_DRAW) {
                if (imdata_len >= imdata_cap) {
                    size_t   new_cap = (imdata_cap == 0) ? 4096 : imdata_cap * 2;
                    uint8_t *tmp     = (uint8_t *)realloc(imdata, new_cap);
                    if (!tmp) {
                        free(imdata);
                        free(data);
                        return 1;
                    }
                    imdata     = tmp;
                    imdata_cap = new_cap;
                }
                imdata[imdata_len++] = b;
                index++;
            }
        }
    }

    /* Flush any trailing draw data at end of file */
    if (index > 0 && imdata_len > 0) {
        decode_and_draw(imdata, imdata_len, target_data[0]);
        imageindex++;
        char path[64];
        snprintf(path, sizeof(path), "imgs/%04d.png", imageindex);
        stbi_write_png(path, WIDTH, HEIGHT, 3, framebuf, WIDTH * 3);
    }

    printf("Saved %d images to imgs/\n", imageindex);

    free(imdata);
    free(data);
    return 0;
}
