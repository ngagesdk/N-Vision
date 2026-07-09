# Nokia N-Gage LCD Protocol Documentation

## Overview

This document describes the LCD bus protocol used in the Nokia N-Gage (NEM-4).
The protocol is a synchronous, command-and-data serial bus that controls a
display with 176×208 pixel resolution and 12-bit color depth (4096 colors).

## Bus Architecture

The LCD bus consists of **9 signal lines**:

- **LCDCLK** - Master clock signal. All data transfers occur on the rising edge.
- **LCDM** - Mode flag (1 bit)
  - `1` = Pixel-data word
  - `0` = Command word
- **LCDDa0–LCDDa6** - 7-bit data bus (bits 0–6, respectively)

### Bus Byte Layout (MSB First)

```
Bit 7: LCDM       – Mode: 1 = pixel-data word, 0 = command
Bit 6: LCDDa6     – Display data bus, bit 6
Bit 5: LCDDa5     – Display data bus, bit 5
Bit 4: LCDDa4     – Display data bus, bit 4
Bit 3: LCDDa3     – Display data bus, bit 3
Bit 2: LCDDa2     – Display data bus, bit 2
Bit 1: LCDDa1     – Display data bus, bit 1
Bit 0: LCDDa0     – Display data bus, bit 0
```

## Command Words (LCDM = 0)

When **LCDM = 0**, bits 6–0 encode a control strobe. The payload is masked as:

```
LCD_PAYLOAD_MASK = 0x7F (bits 6–0)
```

### Defined Commands

#### LCDLLClk - Line Latch Clock (0x75)

- **Value**: `0x75` (binary: `0111_0101`)
- **Purpose**: Loads the target row address into the display controller
- **Followed by**: 4 data words containing row/region parameters:
  - `target_data[0]` - Primary parameter (usually row or starting row)
  - `target_data[1]` - Secondary parameter (reserved)
  - `target_data[2]` - Tertiary parameter (reserved)
  - `target_data[3]` - Quaternary parameter (reserved)

#### LCDDISPClk - Display Pixel Clock (0x5C)

- **Value**: `0x5C` (binary: `0101_1100`)
- **Purpose**: Enters pixel-data transmission mode
- **Followed by**: One or more pixel-data words (LCDM = 1) until the
  next command

## Data Words (LCDM = 1)

When **LCDM = 1**, a data word is transmitted. The payload is:

```
LCD_PAYLOAD_MASK = 0x7F (bits 6–0)
```

### Pixel Data Format

Pixel data is encoded using **4-bit nibbles** (half-bytes).
Each transmitted byte contains **two nibbles**:

- **High nibble** = bits 7–4 (contains `LCDM` + upper data bits)
- **Low nibble** = bits 3–0 (contains lower data bits)

Each nibble represents 4 bits of display information:

```
High nibble: [LCDM | LCDDa6 | LCDDa5 | LCDDa4]
Low nibble:  [LCDDa3 | LCDDa2 | LCDDa1 | LCDDa0]
```

### Color Encoding (12-bit Depth with Interlacing)

The display uses **12-bit RGB color** (4 bits per color channel, 4,096 total colors). The encoding uses an interlaced nibble scheme that interleaves color components across consecutive nibbles:

1. **Three consecutive data nibbles** encode one pixel:
   - `n0` - Red channel (contains R[2:0] as LSBs and R[3] as interlaced bit)
   - `n1` - Green channel (contains G[2:0] as LSBs and G[3] as interlaced bit)
   - `n2` - Blue channel (contains B[2:0] as LSBs and B[3] as interlaced bit)

2. **Interlaced MSB Encoding** (bit 3 in each nibble):

   The MSB of each color component is predicted/selected based on **pixel column position**:
   - **Even columns** (0, 2, 4, ...): 
     - R[3] uses predictor from `n1[3]` (Green's interlaced bit)
     - G[3] uses data from `n1[3]` (local bit)
     - B[3] uses predictor from `n1[3]`
   - **Odd columns** (1, 3, 5, ...):
     - R[3] uses data from `n0[3]` (local bit)
     - G[3] uses data from `n1[3]` (local bit)
     - B[3] uses data from `n2[3]` (local bit)

   This interlacing scheme compresses data by sharing/predicting MSBs across neighboring pixels.

3. **Final RGB Value** (4 bits per channel):

   ```
   R = ((n0 & 0x7) | (predictor_or_local << 3)) * 16  ->  [0, 240] in 8-bit (4-bit -> 8-bit expansion)
   G = ((n1 & 0x7) | (n1[3] << 3)) * 16               ->  [0, 240] in 8-bit (4-bit -> 8-bit expansion)
   B = ((n2 & 0x7) | (predictor_or_local << 3)) * 16  ->  [0, 240] in 8-bit (4-bit -> 8-bit expansion)
   ```

   Note: The interlacing implementation may not be fully optimized for all display patterns.

## Frame Structure

A complete frame transmission follows this sequence:

1. **LCDLLClk command** (0x75 + 4 data words)
   - Sets the target row/region
   - Resets the pixel buffer for the new frame

2. **LCDDISPClk command** (0x5C)
   - Enters pixel-data mode
   - Accumulated pixel data begins

3. **Pixel data words** (LCDM = 1)
   - Multiple bytes containing encoded pixel data
   - Each byte contains 2 nibbles
   - Decodable in groups of 3 consecutive nibbles (1 pixel)

4. **Next command** (LCDLLClk or other)
   - Triggers output of accumulated pixel data
   - If LCDLLClk: new frame starts

## Timing

- All data transfers occur on the **rising edge** of **LCDCLK**
- There is no specified minimum clock rate or setup/hold time in this protocol documentation
- One complete frame requires multiple clock cycles

## Resolution and Output

- **Display dimensions**: 176 × 208 pixels
- **Color depth**: 12-bit RGB (4 bits per channel, 4,096 possible colors)
- **Total pixels per frame**: 36,608
- **Color encoding**: Interlaced nibble scheme (see Color Encoding section)

## Example: Reading a Frame

1. LCDCLK rising edge: LCDLLClk command (0x75) received -> state = LCDLLClk
2. LCDCLK rising edge: Data word 0 (target row) -> target_data[0]
3. LCDCLK rising edge: Data word 1 -> target_data[1]
4. LCDCLK rising edge: Data word 2 -> target_data[2]
5. LCDCLK rising edge: Data word 3 -> target_data[3]
6. LCDCLK rising edge: LCDDISPClk command (0x5C) received -> state = LCDDISPClk, start pixel accumulation
7. LCDCLK rising edge (multiple): Pixel data words received
8. (Next command or end of stream): Trigger decode and frame output
