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

#ifndef MNGR_STA_TEST_MODE
#define MNGR_STA_TEST_MODE 0
#endif
#ifndef MNGR_STA_TEST_SSID
#define MNGR_STA_TEST_SSID ""
#endif
#ifndef MNGR_STA_TEST_PASSWORD
#define MNGR_STA_TEST_PASSWORD ""
#endif
#ifndef MNGR_STA_TEST_AUTH
#define MNGR_STA_TEST_AUTH 0
#endif

int __not_in_flash_func(mngr_init)() {
  // Set SSID
  char ssid[128] = {0};
  SettingsConfigEntry *ssid_param =
      settings_find_entry(gconfig_getContext(), PARAM_WIFI_SSID);
  if (strlen(ssid_param->value) == 0) {
    DPRINTF("No SSID found in config.\n");
    snprintf(ssid, sizeof(ssid), "%s", "No SSID found");
  } else {
    snprintf(ssid, sizeof(ssid), "%s", ssid_param->value);
  }

#if MNGR_STA_TEST_MODE
  wifi_mode_t wifi_mode_value = WIFI_MODE_STA;
#else
  wifi_mode_t wifi_mode_value = WIFI_MODE_AP;
#endif

  int err = network_wifiInit(wifi_mode_value);

#if MNGR_STA_TEST_MODE
  if (wifi_mode_value == WIFI_MODE_STA) {
    // Set the WiFi connection parameters and connect
    settings_put_string(gconfig_getContext(), PARAM_WIFI_SSID,
                        MNGR_STA_TEST_SSID);
    settings_put_string(gconfig_getContext(), PARAM_WIFI_PASSWORD,
                        MNGR_STA_TEST_PASSWORD);
    settings_put_integer(gconfig_getContext(), PARAM_WIFI_AUTH,
                         MNGR_STA_TEST_AUTH);
    DPRINTF("Connecting to WiFi network (STA test mode)...\n");
    // Connect to the WiFi network
    int numRetries = 3;
    err = NETWORK_WIFI_STA_CONN_ERR_TIMEOUT;
    while (err != NETWORK_WIFI_STA_CONN_OK) {
      err = network_wifiStaConnect();
      if (err < 0) {
        DPRINTF("Error connecting to the WiFi network: %i\n", err);
        DPRINTF("Number of retries left: %i\n", numRetries);
        if (--numRetries <= 0) {
          DPRINTF("Max retries reached. Exiting...\n");
          sleep_ms(1000);
          return err;
        }
      }
    }
  }
#endif
  DPRINTF("WiFi connected\n");

  if (err != 0) {
    DPRINTF("Error initializing the network: %i\n", err);
    return err;
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
