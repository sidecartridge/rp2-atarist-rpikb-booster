/**
 * Minimal Bluetooth scan/pair loop.
 * Starts Bluepad32/BTstack, scans for HID peripherals and pairs them.
 */
#ifndef BTLOOP_H
#define BTLOOP_H

#include <stddef.h>

typedef struct {
  char address[18];
  char name[64];
  char type[16];
} bt_device_info_t;

void btloop_enable(void);
void btloop_disable(void);
void btloop_poll(void);
void btloop_get_devices(const bt_device_info_t **devices, size_t *count);
void btloop_reset_devices(void);
void btloop_clear_bt_lists(void);
void btloop_clear_pairings(void);

#endif  // BTLOOP_H
