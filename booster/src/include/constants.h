/**
 * File: constants.h
 * Author: Diego Parrilla Santamaría
 * Date: Novemeber 2024
 * Copyright: 2025 - GOODDATA LABS SL
 * Description: Constants used in the placeholder file
 */

#ifndef CONSTANTS_H
#define CONSTANTS_H

#include "hardware/vreg.h"

// GPIO for the select ATARI or USB keyboard mode
#define KBD_ATARI_OUT_3V3_GPIO 7
#define KBD_USB_OUT_3V3_GPIO 8

// GPIO for the IKBD interface
#define KBD_RESET_IN_3V3_GPIO 3
#define KBD_BD0SEL_3V3_GPIO 6

// Common macros
#define HEX_BASE 16
#define DEC_BASE 10

// Time macros
#define SEC_TO_MS 1000

// Frequency constants.
#define SAMPLE_DIV_FREQ (1.f)         // Sample frequency division factor.
#define RP2040_CLOCK_FREQ_KHZ 125000  // Clock frequency in KHz (225MHz).

// Voltage constants.
#define RP2040_VOLTAGE VREG_VOLTAGE_1_10  // Voltage in 1.10 Volts.
#define VOLTAGE_VALUES                                                 \
  (const char *[]){"NOT VALID", "NOT VALID", "NOT VALID", "NOT VALID", \
                   "NOT VALID", "NOT VALID", "0.85v",     "0.90v",     \
                   "0.95v",     "1.00v",     "1.05v",     "1.10v",     \
                   "1.15v",     "1.20v",     "1.25v",     "1.30v",     \
                   "NOT VALID", "NOT VALID", "NOT VALID", "NOT VALID", \
                   "NOT VALID"}

#define BOOSTER_TITLE "SidecarTridge Croissant"

#define CURRENT_APP_NAME_KEY "BOOSTER"

// Time macros
#define GET_CURRENT_TIME() \
  (((uint64_t)timer_hw->timerawh) << 32u | timer_hw->timerawl)
#define GET_CURRENT_TIME_INTERVAL_MS(start) \
  (uint32_t)((GET_CURRENT_TIME() - start) / \
             (((uint32_t)RP2040_CLOCK_FREQ_KHZ) / 1000))

// NOLINTBEGIN(readability-identifier-naming)
extern unsigned int __flash_binary_start;
extern unsigned int _storage_flash_start;
extern unsigned int _config_flash_start;
extern unsigned int _global_lookup_flash_start;
extern unsigned int _global_config_flash_start;
extern unsigned int __rom_in_ram_start__;
// NOLINTEND(readability-identifier-naming)

#endif  // CONSTANTS_H