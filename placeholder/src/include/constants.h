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

// SELECT signal
#define SELECT_GPIO 5  // GPIO signal for SELECT

// Frequency constants.
#define SAMPLE_DIV_FREQ (1.f)         // Sample frequency division factor.
#define RP2040_CLOCK_FREQ_KHZ 225000  // Clock frequency in KHz (225MHz).

// Voltage constants.
#define RP2040_VOLTAGE VREG_VOLTAGE_1_20  // Voltage in 1.20 Volts.
#define VOLTAGE_VALUES                                                 \
  (const char *[]){"NOT VALID", "NOT VALID", "NOT VALID", "NOT VALID", \
                   "NOT VALID", "NOT VALID", "0.85v",     "0.90v",     \
                   "0.95v",     "1.00v",     "1.05v",     "1.10v",     \
                   "1.15v",     "1.20v",     "1.25v",     "1.30v",     \
                   "NOT VALID", "NOT VALID", "NOT VALID", "NOT VALID", \
                   "NOT VALID"}

// This is the APP KEY that will be used to identify the current app
// It mmust be a unique UUID4 for each app, and must be the one used in the
// app.json file as descriptor of the app
#ifndef CURRENT_APP_UUID_KEY
#define CURRENT_APP_UUID_KEY "PLACEHOLDER"
#endif

// Time macros
#define GET_CURRENT_TIME() \
  (((uint64_t)timer_hw->timerawh) << 32u | timer_hw->timerawl)
#define GET_CURRENT_TIME_INTERVAL_MS(start) \
  (uint32_t)((GET_CURRENT_TIME() - start) / \
             (((uint32_t)RP2040_CLOCK_FREQ_KHZ) / 1000))

// NOLINTBEGIN(readability-identifier-naming)
extern unsigned int __flash_binary_start;
extern unsigned int _storage_flash_start;
extern unsigned int _booster_app_flash_start;
extern unsigned int _config_flash_start;
extern unsigned int _global_lookup_flash_start;
extern unsigned int _global_config_flash_start;
// NOLINTEND(readability-identifier-naming)

#ifndef FLASH_SECTOR_SIZE
#define FLASH_SECTOR_SIZE (1u << 12)
#endif

#endif  // CONSTANTS_H