#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define WIDTH 176
#define HEIGHT 208
#define TOTAL_PIXELS (WIDTH * HEIGHT)

typedef struct {
    uint8_t r, g, b;
} RGB;

// Decode 12-bit BGR444 pixel (OMAP 1510 LCD bus order) to RGB888.
// The OMAP 1510 LCD controller outputs BGR444, not RGB444:
// Bits [11:8]  = B3:B0 (blue,  4 bits)
// Bits [7:4]   = G3:G0 (green, 4 bits)
// Bits [3:0]   = R3:R0 (red,   4 bits)
//
// Expand each 4-bit component to 8-bit:
// Red/Blue: 4-bit -> 5-bit (replicate MSB), then 5-bit -> 8-bit
// Green:    4-bit -> 6-bit (replicate top 2 bits), then 6-bit -> 8-bit
RGB decode_bgr444_to_rgb888(uint16_t data12) {
    RGB color;
    // Extract 4-bit color values from BGR444 bus
    uint8_t b4 = (data12 >> 8) & 0x0F;  // Bits [11:8] -> Blue
    uint8_t g4 = (data12 >> 4) & 0x0F;  // Bits [7:4]  -> Green
    uint8_t r4 = (data12 >> 0) & 0x0F;  // Bits [3:0]  -> Red

    // Expand 4-bit values to 5/6-bit and then to 8-bit
    // Red: 4-bit -> 5-bit by replicating MSB to LSB
    uint8_t r5 = (r4 << 1) | (r4 >> 3);
    color.r = (r5 << 3) | (r5 >> 2);  // Scale 5-bit to 8-bit

    // Green: 4-bit -> 6-bit by replicating top 2 bits to bottom 2
    uint8_t g6 = (g4 << 2) | (g4 >> 2);
    color.g = (g6 << 2) | (g6 >> 4);  // Scale 6-bit to 8-bit

    // Blue: 4-bit -> 5-bit by replicating MSB to LSB
    uint8_t b5 = (b4 << 1) | (b4 >> 3);
    color.b = (b5 << 3) | (b5 >> 2);  // Scale 5-bit to 8-bit

    return color;
}

int main(int argc, char* argv[]) {
    const char* input_file = "trace.csv";
    const char* output_file = "output.png";

    if (argc > 1) {
        input_file = argv[1];
    }
    if (argc > 2) {
        output_file = argv[2];
    }

    printf("LCD Data Decoder (OMAP 1)\n");
    printf("Input: %s\n", input_file);
    printf("Output: %s\n", output_file);
    printf("Resolution: %dx%d (%d pixels)\n\n", WIDTH, HEIGHT, TOTAL_PIXELS);

    // Allocate image buffer (RGB24 for output)
    uint8_t* image = (uint8_t*)malloc(TOTAL_PIXELS * 3);
    if (!image) {
        fprintf(stderr, "Failed to allocate image buffer\n");
        return 1;
    }

    // Open CSV file
    FILE* fp = fopen(input_file, "r");
    if (!fp) {
        fprintf(stderr, "Failed to open input file: %s\n", input_file);
        free(image);
        return 1;
    }

    // Skip header
    char line[2048];
    if (!fgets(line, sizeof(line), fp)) {
        fprintf(stderr, "Failed to read header\n");
        fclose(fp);
        free(image);
        return 1;
    }

    // Pixel data is sampled on every PCLK rising edge (channels[14] in parsed array).
    // Each CSV row is a state-change event; PCLK 0->1 transitions gate valid pixels.
    // Channels 7, 8 and 12 are always low (unused/GND); Ch14 is PCLK.
    // 12 data channels map to 12-bit BGR444 (OMAP 1510 bus order):
    //   B[3:0] = Ch1  (bit 11), Ch15 (bit 10), Ch9  (bit  9), Ch0  (bit  8)
    //   G[3:0] = Ch13 (bit  7), Ch11 (bit  6), Ch10 (bit  5), Ch6  (bit  4)
    //   R[3:0] = Ch5  (bit  3), Ch4  (bit  2), Ch3  (bit  1), Ch2  (bit  0)
    int pixel_count = 0;
    int prev_pclk = -1;

    while (fgets(line, sizeof(line), fp) && pixel_count < TOTAL_PIXELS) {
        // Parse CSV line
        // Format: Time [s], Channel 0-13, PCLK, Channel 15
        double time_val;
        uint16_t channels[17];

        int read = sscanf(line, "%lf,%hu,%hu,%hu,%hu,%hu,%hu,%hu,%hu,%hu,%hu,%hu,%hu,%hu,%hu,%hu,%hu",
                         &time_val,
                         &channels[0], &channels[1], &channels[2], &channels[3],
                         &channels[4], &channels[5], &channels[6], &channels[7],
                         &channels[8], &channels[9], &channels[10], &channels[11],
                         &channels[12], &channels[13], &channels[14], &channels[15]);

        if (read != 17) {
            continue;
        }

        // Sample pixel on PCLK rising edge (channels[14] = PCLK)
        if (prev_pclk == 0 && channels[14] == 1) {
            // Combine 12 data channels into 12-bit BGR444 pixel value
            uint16_t pixel_data = 0;
            uint8_t data_channels_8bit[] = {2, 3, 4, 5, 6, 10, 11, 13};

            for (int i = 0; i < 8; i++) {
                if (channels[data_channels_8bit[i]]) {
                    pixel_data |= (1 << i);
                }
            }

            if (channels[0])  pixel_data |= (1 << 8);
            if (channels[9])  pixel_data |= (1 << 9);
            if (channels[15]) pixel_data |= (1 << 10);
            if (channels[1])  pixel_data |= (1 << 11);

            // Decode to RGB888 and store in image buffer
            RGB color = decode_bgr444_to_rgb888(pixel_data);
            int pos = pixel_count * 3;
            image[pos + 0] = color.r;
            image[pos + 1] = color.g;
            image[pos + 2] = color.b;

            pixel_count++;
        }

        prev_pclk = channels[14];
    }

    fclose(fp);

    printf("Read %d pixel values from CSV\n", pixel_count);

    if (pixel_count < TOTAL_PIXELS) {
        printf("Warning: Expected %d pixels, got %d\n", TOTAL_PIXELS, pixel_count);
        // Fill remaining pixels with black
        for (int i = pixel_count; i < TOTAL_PIXELS; i++) {
            int pos = i * 3;
            image[pos + 0] = 0;
            image[pos + 1] = 0;
            image[pos + 2] = 0;
        }
    }

    // Write PNG file using stb_image_write
    printf("Writing PNG to %s...\n", output_file);
    if (stbi_write_png(output_file, WIDTH, HEIGHT, 3, image, WIDTH * 3)) {
        printf("Successfully wrote image to %s\n", output_file);
    } else {
        fprintf(stderr, "Failed to write PNG file\n");
        free(image);
        return 1;
    }

    free(image);

    printf("Decoding complete!\n");
    return 0;
}
