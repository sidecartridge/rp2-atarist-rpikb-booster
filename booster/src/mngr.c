/**
 * File: mngr.c
 * Author: Diego Parrilla Santamaría
 * Date: December 2024
 * Copyright: 2023-25 - GOODDATA LABS SL
 * Description: C file with the main loop of the manager module
 */

#include "mngr.h"

#include <string.h>

#include "constants.h"
#include "debug.h"
#include "gconfig.h"
#include "include/btloop.h"
#include "mngr_httpd.h"
#include "network.h"
#include "pico/stdlib.h"

#define MNGR_BLINK_PERIOD_MS 500

int __not_in_flash_func(mngr_init)() {
  wifi_mode_t wifi_mode_value = WIFI_MODE_AP;
  SettingsConfigEntry *wifi_mode_param =
      settings_find_entry(gconfig_getContext(), PARAM_WIFI_MODE);
  if (wifi_mode_param != NULL && wifi_mode_param->value != NULL &&
      atoi(wifi_mode_param->value) == 1) {
    wifi_mode_value = WIFI_MODE_STA;
  }

  int err = network_wifiInit(wifi_mode_value);
  if (err != 0) {
    DPRINTF("Error initializing the network: %i\n", err);
    return err;
  }

  if (wifi_mode_value == WIFI_MODE_STA) {
    DPRINTF("Connecting to WiFi network (STA mode)...\n");
    int numRetries = 3;
    int staErr = NETWORK_WIFI_STA_CONN_ERR_TIMEOUT;
    while (staErr != NETWORK_WIFI_STA_CONN_OK && numRetries > 0) {
      staErr = network_wifiStaConnect();
      if (staErr < 0) {
        DPRINTF("Error connecting to WiFi in STA mode: %i\n", staErr);
        DPRINTF("Number of retries left: %i\n", numRetries - 1);
        numRetries--;
        if (numRetries > 0) {
          sleep_ms(1000);
        }
      }
    }
    if (staErr != NETWORK_WIFI_STA_CONN_OK) {
      DPRINTF("STA connection failed after retries.\n");
#ifdef CYW43_WL_GPIO_LED_PIN
      DPRINTF("Falling back to AP mode.\n");
      // Use AP fallback defaults instead of STA credentials.
      settings_put_integer(gconfig_getContext(), PARAM_WIFI_MODE, WIFI_MODE_AP);
      settings_put_string(gconfig_getContext(), PARAM_HOSTNAME, WIFI_AP_HOSTNAME);
      settings_put_string(gconfig_getContext(), PARAM_WIFI_PASSWORD, WIFI_AP_PASS);
      settings_put_integer(gconfig_getContext(), PARAM_WIFI_AUTH, WIFI_AP_AUTH);
      err = settings_save(gconfig_getContext(), true);
      if (err != 0) {
        DPRINTF("Error saving AP fallback settings: %i\n", err);
      }
      network_deInit();
      err = network_wifiInit(WIFI_MODE_AP);
      if (err != 0) {
        DPRINTF("Error initializing AP fallback: %i\n", err);
        return err;
      }
#else
      return staErr;
#endif
    } else {
      DPRINTF("WiFi STA connected.\n");
    }
  } else {
    DPRINTF("WiFi AP ready.\n");
  }

  // Bluetooth scanning remains disabled until explicitly started via CGI.
  btloop_disable();
  mngr_httpd_start();

  return 0;
}

void __not_in_flash_func(mngr_loop)() {
  // Blink status: keep ATARI steady and blink only the USB output.
  bool usb_active = false;
  absolute_time_t next_blink = make_timeout_time_ms(MNGR_BLINK_PERIOD_MS);

  while (true) {
#if PICO_CYW43_ARCH_POLL
    network_safePoll();
    cyw43_arch_wait_for_work_until(make_timeout_time_ms(10));
#else
    sleep_ms(10);
#endif
    btloop_poll();
    if (absolute_time_diff_us(get_absolute_time(), next_blink) <= 0) {
      usb_active = !usb_active;
      gpio_put(KBD_USB_OUT_3V3_GPIO, usb_active);
      next_blink = make_timeout_time_ms(MNGR_BLINK_PERIOD_MS);
    }
  }
}
