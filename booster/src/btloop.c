#include "include/btloop.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ble/sm.h"  // or <btstack/src/ble/sm.h> depending on include paths
#include "btstack.h"
#include "btstack_util.h"
#include "debug.h"
#include "gconfig.h"
#include "pico/async_context.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "uni.h"

typedef struct {
  const char *param;
  bd_addr_t addr;
  bool valid;
} bt_allow_entry_t;

static bt_allow_entry_t bt_allow_entries[] = {
    {PARAM_BT_KEYBOARD, {0}, false},
    {PARAM_BT_MOUSE, {0}, false},
    {PARAM_BT_GAMEPAD, {0}, false},
};

static bt_device_info_t bt_devices[16];
static size_t bt_devices_count = 0;

static void btloop_reset_devices_internal(void) {
  memset(bt_devices, 0, sizeof(bt_devices));
  bt_devices_count = 0;
}

static const char *bt_class_to_type(uint16_t cod) {
  const uint16_t minor = cod & UNI_BT_COD_MINOR_MASK;
  if (minor & UNI_BT_COD_MINOR_KEYBOARD_AND_MICE) return "Keyboard/Mouse";
  if (minor & UNI_BT_COD_MINOR_KEYBOARD) return "Keyboard";
  if (minor & UNI_BT_COD_MINOR_MICE) return "Mouse";
  if (minor & (UNI_BT_COD_MINOR_GAMEPAD | UNI_BT_COD_MINOR_JOYSTICK))
    return "Gamepad";
  return "Unknown";
}

static void btloop_store_device(bd_addr_t addr, const char *name, uint16_t cod,
                                const char *type_override) {
  char addr_str[18];
  snprintf(addr_str, sizeof(addr_str), "%02X:%02X:%02X:%02X:%02X:%02X", addr[0],
           addr[1], addr[2], addr[3], addr[4], addr[5]);

  const char *type = (type_override && type_override[0])
                         ? type_override
                         : bt_class_to_type(cod);

  for (size_t i = 0; i < bt_devices_count; i++) {
    if (strcmp(bt_devices[i].address, addr_str) == 0) {
      snprintf(bt_devices[i].name, sizeof(bt_devices[i].name), "%s",
               (name && name[0]) ? name : "Unknown");
      snprintf(bt_devices[i].type, sizeof(bt_devices[i].type), "%s", type);
      return;
    }
  }

  if (bt_devices_count < (sizeof(bt_devices) / sizeof(bt_devices[0]))) {
    bt_device_info_t *slot = &bt_devices[bt_devices_count++];
    snprintf(slot->address, sizeof(slot->address), "%s", addr_str);
    snprintf(slot->name, sizeof(slot->name), "%s",
             (name && name[0]) ? name : "Unknown");
    snprintf(slot->type, sizeof(slot->type), "%s", type);
  }
}

static void btloop_persist_pairing(const char *addr_str, const char *type,
                                   const char *name) {
  if (addr_str == NULL || addr_str[0] == '\0') return;

  char combined[128];
  const char *dev_name = (name && name[0]) ? name : "Unknown";
  snprintf(combined, sizeof(combined), "%s#%s", addr_str, dev_name);

  bool saved = false;
  if (type && strstr(type, "Keyboard")) {
    settings_put_string(gconfig_getContext(), PARAM_BT_KEYBOARD, combined);
    saved = true;
  }
  if (type && strstr(type, "Mouse")) {
    settings_put_string(gconfig_getContext(), PARAM_BT_MOUSE, combined);
    saved = true;
  }
  if (type && strstr(type, "Gamepad")) {
    settings_put_string(gconfig_getContext(), PARAM_BT_GAMEPAD, combined);
    saved = true;
  }
  if (saved) {
    settings_save(gconfig_getContext(), true);
  }
}

static void load_bt_allowlist_entries(void) {
  for (size_t i = 0;
       i < (sizeof(bt_allow_entries) / sizeof(bt_allow_entries[0])); ++i) {
    bt_allow_entries[i].valid = false;
    SettingsConfigEntry *entry =
        settings_find_entry(gconfig_getContext(), bt_allow_entries[i].param);
    if (entry == NULL || entry->value == NULL || entry->value[0] == '\0') {
      continue;
    }
    if (sscanf_bd_addr(entry->value, bt_allow_entries[i].addr) == 1) {
      bt_allow_entries[i].valid = true;
      DPRINTF("Loaded BD_ADDR for %s: %s\n", bt_allow_entries[i].param,
              bd_addr_to_str(bt_allow_entries[i].addr));
    } else {
      DPRINTF("Invalid BD_ADDR for %s: '%s'\n", bt_allow_entries[i].param,
              entry->value);
    }
  }
}

static void btloop_init(int argc, const char **argv) {
  ARG_UNUSED(argc);
  ARG_UNUSED(argv);
  DPRINTF("btloop_init\n");
}

static void btloop_on_init_complete(void) {
  DPRINTF("btloop: init complete\n");
  btloop_reset_devices_internal();
  for (size_t i = 0;
       i < (sizeof(bt_allow_entries) / sizeof(bt_allow_entries[0])); ++i) {
    if (bt_allow_entries[i].valid) {
      uni_bt_allowlist_add_addr(bt_allow_entries[i].addr);
    }
  }
  uni_bt_allowlist_list();
  uni_bt_list_keys_unsafe();
  uni_bt_start_scanning_and_autoconnect_unsafe();
}

static uni_error_t btloop_on_device_discovered(bd_addr_t addr, const char *name,
                                               uint16_t cod, uint8_t rssi) {
  DPRINTF("Discovered %s name='%s' cod=0x%06x rssi=%d\n", bd_addr_to_str(addr),
          name ? name : "<null>", (unsigned)cod, (int8_t)rssi);
  // Allow HID peripherals (keyboard/mouse/gamepad/joystick), ignore others.
  const uint16_t allowed_minor =
      UNI_BT_COD_MINOR_KEYBOARD_AND_MICE | UNI_BT_COD_MINOR_KEYBOARD |
      UNI_BT_COD_MINOR_MICE | UNI_BT_COD_MINOR_GAMEPAD |
      UNI_BT_COD_MINOR_JOYSTICK;
  bool is_peripheral =
      (cod & UNI_BT_COD_MAJOR_MASK) == UNI_BT_COD_MAJOR_PERIPHERAL;
  bool is_keyboard_mouse = (cod & UNI_BT_COD_MINOR_MASK & allowed_minor) != 0;
  btloop_store_device(addr, name, cod, NULL);
  return (is_peripheral && is_keyboard_mouse) ? UNI_ERROR_SUCCESS
                                              : UNI_ERROR_IGNORE_DEVICE;
}

static void btloop_on_device_connected(uni_hid_device_t *d) {
  DPRINTF("Device connected: %p\n", d);
  uni_bt_list_keys_safe();

  bd_addr_t entry_address;

  for (int i = 0; i < le_device_db_max_count(); i++) {
    int entry_address_type = BD_ADDR_TYPE_UNKNOWN;
    sm_key_t irk;  // 16-byte IRK buffer

    le_device_db_info(i, &entry_address_type, entry_address, irk);

    // skip unused entries
    if (entry_address_type == BD_ADDR_TYPE_UNKNOWN) continue;

    DPRINTF("%s - type %u\n", bd_addr_to_str(entry_address),
            entry_address_type);

    DPRINTFRAW("  IRK: ");
    for (int j = 0; j < 16; j++) DPRINTFRAW("%02x ", irk[j]);
    DPRINTFRAW("\n");
  }
}

static void btloop_on_device_disconnected(uni_hid_device_t *d) {
  DPRINTF("Device disconnected: %p\n", d);
  uni_bt_list_keys_safe();
}

static uni_error_t btloop_on_device_ready(uni_hid_device_t *d) {
  DPRINTF("Device ready (paired): %p\n", d);
  bd_addr_t addr = {0};
  uni_bt_conn_get_address(&d->conn, addr);
  const char *type_override = NULL;
  if (uni_hid_device_is_keyboard(d)) {
    type_override = "Keyboard";
  } else if (uni_hid_device_is_mouse(d)) {
    type_override = "Mouse";
  } else if (uni_hid_device_is_gamepad(d)) {
    type_override = "Gamepad";
  }
  btloop_store_device(addr, d->name, d->cod, type_override);
  char addr_str[18];
  snprintf(addr_str, sizeof(addr_str), "%02X:%02X:%02X:%02X:%02X:%02X", addr[0],
           addr[1], addr[2], addr[3], addr[4], addr[5]);
  btloop_persist_pairing(
      addr_str, type_override ? type_override : bt_class_to_type(d->cod),
      d->name);
  uni_bt_list_keys_safe();
  return UNI_ERROR_SUCCESS;
}

static void btloop_on_oob_event(uni_platform_oob_event_t event, void *data) {
  switch (event) {
    case UNI_PLATFORM_OOB_BLUETOOTH_ENABLED:
      DPRINTF("Bluetooth enabled: %d\n", (bool)data);
      break;
    default:
      break;
  }
}

static const uni_property_t *btloop_get_property(uni_property_idx_t idx) {
  ARG_UNUSED(idx);
  return NULL;
}

static struct uni_platform *btloop_platform(void) {
  static struct uni_platform plat = {
      .name = "BT Scanner",
      .init = btloop_init,
      .on_init_complete = btloop_on_init_complete,
      .on_device_discovered = btloop_on_device_discovered,
      .on_device_connected = btloop_on_device_connected,
      .on_device_disconnected = btloop_on_device_disconnected,
      .on_device_ready = btloop_on_device_ready,
      .on_oob_event = btloop_on_oob_event,
      .get_property = btloop_get_property,
  };
  return &plat;
}

static bool btloop_active = false;
static bool btloop_initialized = false;

void btloop_enable(void) {
  if (!btloop_initialized) {
    load_bt_allowlist_entries();
    // btloop_apply_bt_mode();
    uni_platform_set_custom(btloop_platform());
    uni_init(0, NULL);
    btloop_initialized = true;
  }
  btloop_reset_devices_internal();
  btloop_active = true;
}

void btloop_disable(void) { btloop_active = false; }

void btloop_poll(void) {
  if (!btloop_active || !btloop_initialized) {
    return;
  }
  async_context_poll(cyw43_arch_async_context());
  tight_loop_contents();
}

void btloop_get_devices(const bt_device_info_t **devices, size_t *count) {
  if (devices != NULL) {
    *devices = bt_devices;
  }
  if (count != NULL) {
    *count = bt_devices_count;
  }
}

void btloop_reset_devices(void) { btloop_reset_devices_internal(); }

void btloop_clear_pairings(void) {
  uni_bt_del_keys_unsafe();
  uni_bt_le_delete_bonded_keys();
  btloop_reset_devices_internal();
  settings_put_string(gconfig_getContext(), PARAM_BT_KEYBOARD, "");
  settings_put_string(gconfig_getContext(), PARAM_BT_MOUSE, "");
  settings_put_string(gconfig_getContext(), PARAM_BT_GAMEPAD, "");
  settings_save(gconfig_getContext(), true);
}
