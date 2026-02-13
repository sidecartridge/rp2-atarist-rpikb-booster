/**
 * File: main.c
 * Author: Diego Parrilla Santamaría
 * Date: Novemeber 2023
 * Copyright: 2023 - GOODDATA LABS SL
 * Description: C file that launches the ROM emulator
 */

#include "constants.h"
#include "debug.h"
#include "gconfig.h"
#include "mngr.h"
#include "network.h"
#include "pico/btstack_flash_bank.h"

int main() {
  // Set the clock frequency. 20% overclocking
  set_sys_clock_khz(RP2040_CLOCK_FREQ_KHZ, true);

  // Set the voltage
  vreg_set_voltage(RP2040_VOLTAGE);

#if defined(_DEBUG) && (_DEBUG != 0)
  // Initialize chosen serial port
  stdio_init_all();
  setvbuf(stdout, NULL, _IOFBF, 256);

  // Only startup information to display
  DPRINTF("SidecarTridge IKBD Emulator. %s (%s). %s mode.\n\n", RELEASE_VERSION,
          RELEASE_DATE, _DEBUG ? "DEBUG" : "RELEASE");

  // Show information about the frequency and voltage
  int current_clock_frequency_khz = RP2040_CLOCK_FREQ_KHZ;
  const char* current_voltage = VOLTAGE_VALUES[RP2040_VOLTAGE];
  DPRINTF("Clock frequency: %i KHz\n", current_clock_frequency_khz);
  DPRINTF("Voltage: %s\n", current_voltage);
  DPRINTF("PICO_FLASH_SIZE_BYTES: %i\n", PICO_FLASH_SIZE_BYTES);
  DPRINTF("PICO_FLASH_BANK_STORAGE_OFFSET: 0x%X\n",
          (unsigned int)PICO_FLASH_BANK_STORAGE_OFFSET);
  DPRINTF("PICO_FLASH_BANK_TOTAL_SIZE: %u bytes\n",
          (unsigned int)PICO_FLASH_BANK_TOTAL_SIZE);

  unsigned int flash_length =
      (unsigned int)&_config_flash_start - (unsigned int)&__flash_binary_start;
  unsigned int booster_flash_length = flash_length;
  unsigned int config_flash_length = (unsigned int)&_global_lookup_flash_start -
                                     (unsigned int)&_config_flash_start;
  unsigned int global_lookup_flash_length = FLASH_SECTOR_SIZE;
  unsigned int global_config_flash_length = FLASH_SECTOR_SIZE;
  unsigned int bt_tlv_flash_length = (unsigned int)PICO_FLASH_BANK_TOTAL_SIZE;
  unsigned int bt_tlv_flash_start =
      (unsigned int)(XIP_BASE + PICO_FLASH_BANK_STORAGE_OFFSET);

  assert(PICO_FLASH_BANK_STORAGE_OFFSET == (bt_tlv_flash_start - XIP_BASE));
  assert(PICO_FLASH_BANK_TOTAL_SIZE == bt_tlv_flash_length);

  DPRINTF("Flash start: 0x%X, length: %u bytes\n",
          (unsigned int)&__flash_binary_start, flash_length);
  DPRINTF("Booster Flash start: 0x%X, length: %u bytes\n",
          (unsigned int)&__flash_binary_start, booster_flash_length);
  DPRINTF("Config Flash start: 0x%X, length: %u bytes\n",
          (unsigned int)&_config_flash_start, config_flash_length);
  DPRINTF("Global Lookup Flash start: 0x%X, length: %u bytes\n",
          (unsigned int)&_global_lookup_flash_start,
          global_lookup_flash_length);
  DPRINTF("Global Config Flash start: 0x%X, length: %u bytes\n",
          (unsigned int)&_global_config_flash_start,
          global_config_flash_length);
  DPRINTF("BT TLV Flash start: 0x%X, length: %u bytes\n", bt_tlv_flash_start,
          bt_tlv_flash_length);

#endif

  // Configure the output pins
  gpio_init(KBD_ATARI_OUT_3V3_GPIO);
  gpio_set_dir(KBD_ATARI_OUT_3V3_GPIO, GPIO_OUT);
  gpio_put(KBD_ATARI_OUT_3V3_GPIO, 1);

  gpio_init(KBD_USB_OUT_3V3_GPIO);
  gpio_set_dir(KBD_USB_OUT_3V3_GPIO, GPIO_OUT);
  gpio_put(KBD_USB_OUT_3V3_GPIO, 0);

  // Load the global configuration parameters
  int err = gconfig_init(NULL);
  if (err != GCONFIG_SUCCESS) {
    // Let's create the default configuration
    err = settings_save(gconfig_getContext(), true);
    if (err != 0) {
      DPRINTF("Error initializing the global configuration manager: %i\n", err);
      return err;
    }
    // Deinit the settings
    settings_deinit(gconfig_getContext());

    // Start again with the default configuration
    int err = gconfig_init(NULL);
    if (err != GCONFIG_SUCCESS) {
      DPRINTF("Cannot initialize the global configuration manager: %i\n", err);
      return err;
    }
    DPRINTF("Global configuration initialized\n");
  }
  // If we are here, the next time we boot in IKBD mode
  settings_put_string(gconfig_getContext(), PARAM_BOOT_FEATURE, "IKBD");
  settings_save(gconfig_getContext(), true);
  DPRINTF("Boot feature set to IKBD for next boot\n");
  mngr_init();
  mngr_loop();
  return 0;
}
