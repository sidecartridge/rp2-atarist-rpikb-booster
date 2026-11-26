// units: booster/src/settings/settings.c tests/host/shim/host_flash.c
/* The settings store both firmwares share: the 127-byte entry layout, the
 * MAGICVERSION entry first, what a wrong magic, a stored key the firmware
 * does not know and bytes after the last entry do on load, and the 96-byte
 * value limit. The flash is host_flash, at the offset the firmware uses. */
#include <stddef.h>
#include <string.h>

#include "settings.h"
#include "test.h"

#define OFFSET 0x1FD000u
#define SIZE 4096u
#define MAGIC 0x1234
#define VERSION 1
#define ENTRY sizeof(SettingsConfigEntry)

_Static_assert(sizeof(SettingsConfigEntry) == 127, "a stored entry is 127 bytes");
_Static_assert(offsetof(SettingsConfigEntry, dataType) == 30, "type after the key");
_Static_assert(offsetof(SettingsConfigEntry, value) == 31, "value after the type");

static const SettingsConfigEntry defaults[] = {
    {"ALPHA", SETTINGS_TYPE_STRING, "a"},
    {"COUNT", SETTINGS_TYPE_INT, "1"},
    {"FLAG", SETTINGS_TYPE_BOOL, "false"},
};
#define NUM_DEFAULTS (sizeof(defaults) / sizeof(defaults[0]))

static const uint8_t *stored(size_t entry, size_t field) {
  return host_flash + OFFSET + entry * ENTRY + field;
}

static const char *value(SettingsContext *ctx, const char *key) {
  SettingsConfigEntry *e = settings_find_entry(ctx, key);
  return e ? e->value : "(missing)";
}

static int load(SettingsContext *ctx, const SettingsConfigEntry *table,
                uint16_t count, uint16_t version) {
  settings_deinit(ctx);
  return settings_init(ctx, table, count, OFFSET, SIZE, MAGIC, version);
}

int main(void) {
  SettingsContext ctx = {0};
  memset(host_flash, 0xFF, sizeof(host_flash));

  // Erased flash: no magic, so the defaults, behind the MAGICVERSION entry.
  CHECK_EQ(load(&ctx, defaults, NUM_DEFAULTS, VERSION), -1);
  CHECK_EQ(ctx.configData.count, NUM_DEFAULTS + 1);
  CHECK(strcmp(ctx.configData.entries[0].key, "MAGICVERSION") == 0);
  CHECK(strcmp(ctx.configData.entries[0].value, "305397761") == 0);  // 0x12340001
  CHECK(strcmp(value(&ctx, "ALPHA"), "a") == 0);

  // Saved layout: 30-byte key, 1-byte type, 96-byte value, MAGICVERSION first.
  CHECK_EQ(settings_put_string(&ctx, "ALPHA", "hello"), 0);
  CHECK_EQ(settings_put_integer(&ctx, "COUNT", -42), 0);
  CHECK_EQ(settings_put_bool(&ctx, "FLAG", true), 0);
  CHECK_EQ(settings_save(&ctx, true), 0);
  CHECK(memcmp(stored(0, 0), "MAGICVERSION", 13) == 0);
  CHECK_EQ(*stored(0, 30), SETTINGS_TYPE_INT);
  CHECK(strcmp((const char *)stored(0, 31), "305397761") == 0);
  CHECK(strcmp((const char *)stored(1, 0), "ALPHA") == 0);
  CHECK_EQ(*stored(1, 30), SETTINGS_TYPE_STRING);
  CHECK(strcmp((const char *)stored(1, 31), "hello") == 0);
  CHECK(strcmp((const char *)stored(3, 31), "true") == 0);

  // Loaded back as saved.
  CHECK_EQ(load(&ctx, defaults, NUM_DEFAULTS, VERSION), (int)NUM_DEFAULTS + 1);
  CHECK(strcmp(value(&ctx, "ALPHA"), "hello") == 0);
  CHECK(strcmp(value(&ctx, "COUNT"), "-42") == 0);
  CHECK(strcmp(value(&ctx, "FLAG"), "true") == 0);

  // A firmware with another version reads none of it: defaults again.
  CHECK_EQ(load(&ctx, defaults, NUM_DEFAULTS, VERSION + 1), -1);
  CHECK(strcmp(value(&ctx, "ALPHA"), "a") == 0);

  // An empty key ends the stored entries: a valid entry after it is not read.
  CHECK_EQ(load(&ctx, defaults, NUM_DEFAULTS, VERSION), (int)NUM_DEFAULTS + 1);
  SettingsConfigEntry end = {"", SETTINGS_TYPE_INT, ""};
  SettingsConfigEntry extra = {"ALPHA", SETTINGS_TYPE_STRING, "after the end"};
  memcpy(host_flash + OFFSET + (NUM_DEFAULTS + 1) * ENTRY, &end, ENTRY);
  memcpy(host_flash + OFFSET + (NUM_DEFAULTS + 2) * ENTRY, &extra, ENTRY);
  CHECK_EQ(load(&ctx, defaults, NUM_DEFAULTS, VERSION), (int)NUM_DEFAULTS + 1);
  CHECK(strcmp(value(&ctx, "ALPHA"), "hello") == 0);

  // Values: 95 characters fit, longer ones are cut to 95.
  char text[200];
  memset(text, 'v', sizeof(text) - 1);
  text[sizeof(text) - 1] = '\0';
  text[95] = '\0';
  CHECK_EQ(settings_put_string(&ctx, "ALPHA", text), 0);
  CHECK_EQ(strlen(value(&ctx, "ALPHA")), 95);
  text[95] = 'v';
  text[96] = '\0';
  CHECK_EQ(settings_put_string(&ctx, "ALPHA", text), 0);
  CHECK_EQ(strlen(value(&ctx, "ALPHA")), 95);
  text[96] = 'v';
  CHECK_EQ(settings_put_string(&ctx, "ALPHA", text), 0);
  CHECK_EQ(strlen(value(&ctx, "ALPHA")), 95);
  CHECK_EQ(settings_save(&ctx, true), 0);
  CHECK_EQ(load(&ctx, defaults, NUM_DEFAULTS, VERSION), (int)NUM_DEFAULTS + 1);
  CHECK_EQ(strlen(value(&ctx, "ALPHA")), 95);

  // Only keys the defaults have can be written, and only in the key format.
  CHECK_EQ(settings_put_string(&ctx, "UNKNOWN", "x"), -1);
  CHECK_EQ(settings_put_string(&ctx, "alpha", "x"), -1);
  CHECK(settings_find_entry(&ctx, "UNKNOWN") == NULL);

  // A stored key the running firmware does not know is dropped on load and
  // gone after its next save. Here it is stored last, after every known key.
  static const SettingsConfigEntry withExtra[] = {
      {"ALPHA", SETTINGS_TYPE_STRING, "a"},
      {"COUNT", SETTINGS_TYPE_INT, "1"},
      {"FLAG", SETTINGS_TYPE_BOOL, "false"},
      {"OTHER", SETTINGS_TYPE_STRING, "only in the other firmware"},
  };
  memset(host_flash + OFFSET, 0xFF, SIZE);
  load(&ctx, withExtra, 4, VERSION);
  CHECK_EQ(settings_put_string(&ctx, "ALPHA", "kept"), 0);
  CHECK_EQ(settings_save(&ctx, true), 0);
  CHECK_EQ(load(&ctx, defaults, NUM_DEFAULTS, VERSION), (int)NUM_DEFAULTS + 1);
  CHECK(strcmp(value(&ctx, "ALPHA"), "kept") == 0);
  CHECK(settings_find_entry(&ctx, "OTHER") == NULL);
  CHECK_EQ(settings_save(&ctx, true), 0);
  for (size_t i = 0; i <= NUM_DEFAULTS; i++) {
    CHECK(strcmp((const char *)stored(i, 0), "OTHER") != 0);
  }

  settings_deinit(&ctx);
  TEST_END();
}
