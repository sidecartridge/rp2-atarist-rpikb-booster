// units: booster/src/settings/settings.c tests/host/shim/host_flash.c
/* After the last entry, settings_save() leaves zeros or erased flash, not
 * the RAM contents of its buffer. And when the running firmware has more
 * defaults than the stored settings have entries (any upgrade that adds a
 * key), load does not read whatever follows the last entry as one more: a
 * key with no terminator inside its 30 bytes ends the entries. The tail is
 * set here to such bytes, 'A's with no terminator, as an older firmware's
 * save could have left it. */
#include <string.h>

#include "settings.h"
#include "test.h"

#define OFFSET 0x1FD000u
#define SIZE 4096u
#define ENTRY sizeof(SettingsConfigEntry)

int main(void) {
  static const SettingsConfigEntry before[] = {
      {"ALPHA", SETTINGS_TYPE_STRING, "a"},
  };
  static const SettingsConfigEntry after[] = {
      {"ALPHA", SETTINGS_TYPE_STRING, "a"},
      {"ADDED", SETTINGS_TYPE_STRING, "new key"},
  };
  SettingsContext ctx = {0};
  memset(host_flash, 0xFF, sizeof(host_flash));

  settings_init(&ctx, before, 1, OFFSET, SIZE, 0x1234, 1);
  settings_save(&ctx, true);
  settings_deinit(&ctx);
  // Save leaves no RAM contents after the last entry: the rest of the
  // sector is all 0xFF (erased) or all 0x00.
  const uint8_t *tail = host_flash + OFFSET + 2 * ENTRY;
  size_t erased = 0, zeroed = 0;
  for (size_t i = 0; i < SIZE - 2 * ENTRY; i++) {
    erased += tail[i] == 0xFF;
    zeroed += tail[i] == 0x00;
  }
  CHECK(erased == SIZE - 2 * ENTRY || zeroed == SIZE - 2 * ENTRY);

  memset(host_flash + OFFSET + 2 * ENTRY, 'A', ENTRY);
  settings_init(&ctx, after, 2, OFFSET, SIZE, 0x1234, 1);
  CHECK(strcmp(settings_find_entry(&ctx, "ADDED")->value, "new key") == 0);
  settings_deinit(&ctx);
  TEST_END();
}
