// units: booster/src/settings/settings.c tests/host/shim/host_flash.c
/* A stored key the running firmware does not know is skipped without taking
 * the place of one it knows: stored before the known keys, it does not stop
 * the last of them from loading. Load reads until the first empty or invalid
 * entry, up to the sector's capacity, not as many entries as it has
 * defaults. */
#include <string.h>

#include "settings.h"
#include "test.h"

#define OFFSET 0x1FD000u
#define SIZE 4096u

int main(void) {
  static const SettingsConfigEntry writer[] = {
      {"OTHER", SETTINGS_TYPE_STRING, "only in the writer"},
      {"ALPHA", SETTINGS_TYPE_STRING, "a"},
      {"OMEGA", SETTINGS_TYPE_STRING, "z"},
  };
  static const SettingsConfigEntry reader[] = {
      {"ALPHA", SETTINGS_TYPE_STRING, "a"},
      {"OMEGA", SETTINGS_TYPE_STRING, "z"},
  };
  SettingsContext ctx = {0};
  memset(host_flash, 0xFF, sizeof(host_flash));

  settings_init(&ctx, writer, 3, OFFSET, SIZE, 0x1234, 1);
  settings_put_string(&ctx, "ALPHA", "saved alpha");
  settings_put_string(&ctx, "OMEGA", "saved omega");
  settings_save(&ctx, true);
  settings_deinit(&ctx);

  settings_init(&ctx, reader, 2, OFFSET, SIZE, 0x1234, 1);
  CHECK(strcmp(settings_find_entry(&ctx, "ALPHA")->value, "saved alpha") == 0);
  CHECK(strcmp(settings_find_entry(&ctx, "OMEGA")->value, "saved omega") == 0);
  settings_deinit(&ctx);
  TEST_END();
}
