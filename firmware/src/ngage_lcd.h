/** @file ngage_lcd.h
 *
 *  PIO-based N-Gage LCD bus decoder - pin configuration and public API.
 *
 *  Pin assignment
 *  --------------
 *  DATA PINS  Eight consecutive GPIOs starting at NGAGE_LCD_DATA_BASE_PIN.
 *             Wire MSB-first so the byte captured by the PIO state machine
 *             matches the raw bus word directly:
 *
 *               NGAGE_LCD_DATA_BASE_PIN + 7  =  LCDDa7 (data bus bit 7)
 *               NGAGE_LCD_DATA_BASE_PIN + 6  =  LCDDa6 (data bus bit 6)
 *               NGAGE_LCD_DATA_BASE_PIN + 5  =  LCDDa5 (data bus bit 5)
 *               NGAGE_LCD_DATA_BASE_PIN + 4  =  LCDDa4 (data bus bit 4)
 *               NGAGE_LCD_DATA_BASE_PIN + 3  =  LCDDa3 (data bus bit 3)
 *               NGAGE_LCD_DATA_BASE_PIN + 2  =  LCDDa2 (data bus bit 2)
 *               NGAGE_LCD_DATA_BASE_PIN + 1  =  LCDDa1 (data bus bit 1)
 *               NGAGE_LCD_DATA_BASE_PIN + 0  =  LCDDa0 (data bus bit 0)
 *
 *  CLOCK PIN  Any GPIO; fully independent of the data pin group.
 *             The PIO detects rising edges and is not restricted to being
 *             adjacent to the data pins.
 *
 *  LCDM PIN   Any GPIO; the mode/control signal. Sampled alongside clock
 *             to determine if the bus word is a command (LCDM=0) or pixel
 *             data (LCDM=1). Independent of both clock and data pins.
 *
 *  Change the four defines below to match your hardware wiring.
 *
 **/

#ifndef NGAGE_LCD_H
#define NGAGE_LCD_H

#include "hardware/pio.h"
#include "ngage_lcd.pio.h"

/* --- Configurable pin assignments --- */

/** First of the 8 consecutive data GPIOs (see wiring table above). */
#define NGAGE_LCD_DATA_BASE_PIN 0u

/** Clock GPIO - any free pin, sampled for rising edges. */
#define NGAGE_LCD_CLK_PIN 8u

/** Mode GPIO - separate LCDM signal, sampled alongside data pins. */
#define NGAGE_LCD_LCDM_PIN 9u

/* --- PIO resource assignment --- */

/** PIO instance used for LCD bus capture. pio0 is reserved for DVI. */
#define NGAGE_LCD_PIO pio1

/** State machine index within NGAGE_LCD_PIO. */
#define NGAGE_LCD_SM 0u

/**
 * Initialise the PIO state machine for continuous LCD bus capture.
 * Must be called once before the main loop, after DVI initialisation.
 */
void ngage_lcd_init(void);

#endif /* NGAGE_LCD_H */
