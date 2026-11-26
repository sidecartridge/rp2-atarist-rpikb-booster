// units: booster/src/httpd_params.c booster/src/settings/settings.c booster/src/cjson/cJSON.c pico-sdk/lib/mbedtls/library/base64.c tests/host/shim/host_flash.c
/* What /saveparams.cgi does to its json parameter (URL decoding, base64,
 * the JSON entries and their special cases) and the text helpers the web
 * pages use, at their edges. Output buffers are heap blocks of the exact size
 * the call is given, so a write past the end fails under ASan. */
#include <stdlib.h>
#include <string.h>

#include "gconfig.h"
#include "httpd_params.h"
#include "mbedtls/base64.h"
#include "test.h"

#define OFFSET 0x1FD000u
#define SIZE 4096u
#define BUF 4096u  // the firmware's url_decoded_param and output_buffer

static SettingsContext ctx;
static char url_buf[BUF], out_buf[BUF], message[128];

static const SettingsConfigEntry defaults[] = {
    {PARAM_MODE, SETTINGS_TYPE_INT, "255"},
    {PARAM_USB_KB_LAYOUT, SETTINGS_TYPE_STRING, "us"},
    {PARAM_WIFI_SSID, SETTINGS_TYPE_STRING, ""},
    {PARAM_FACTORY_MODE, SETTINGS_TYPE_INT, "0"},
    {"HIDDEN_FLAG", SETTINGS_TYPE_BOOL, "false"},
};

static void fresh_settings(void) {
  settings_deinit(&ctx);
  memset(host_flash, 0xFF, sizeof(host_flash));
  settings_init(&ctx, defaults, sizeof(defaults) / sizeof(defaults[0]), OFFSET,
                SIZE, 0x1234, 1);
}

static const char *value(const char *key) {
  SettingsConfigEntry *e = settings_find_entry(&ctx, key);
  return e ? e->value : "(missing)";
}

static int saved(void) { return host_flash[OFFSET] != 0xFF; }

/* The json parameter as the web page sends it: base64, then URL-encoded. */
static char *encode(const char *json) {
  static char param[3 * BUF];
  unsigned char b64[2 * BUF];
  size_t len = 0;
  mbedtls_base64_encode(b64, sizeof(b64), &len, (const unsigned char *)json,
                        strlen(json));
  char *p = param;
  for (size_t i = 0; i < len; i++) {
    if (b64[i] == '+' || b64[i] == '/' || b64[i] == '=') {
      p += snprintf(p, 4, "%%%02X", b64[i]);
    } else {
      *p++ = (char)b64[i];
    }
  }
  *p = '\0';
  return param;
}

static saveparams_result_t apply(const char *param) {
  return saveparams_apply(&ctx, param, url_buf, sizeof(url_buf), out_buf,
                          sizeof(out_buf), message, sizeof(message));
}

static int url_is(const char *src, size_t dst_len, const char *expected) {
  char *dst = malloc(dst_len);
  int ok = url_decode(src, dst, dst_len) == 0 && strcmp(dst, expected) == 0;
  free(dst);
  return ok;
}

static int url_fails(const char *src, size_t dst_len) {
  char *dst = malloc(dst_len);
  int failed = url_decode(src, dst, dst_len) == -1;
  free(dst);
  return failed;
}

static int escaped_is(const char *src, size_t dst_size, const char *expected) {
  char *dst = malloc(dst_size);
  size_t n = json_escape_string(src, dst, dst_size);
  int ok = strcmp(dst, expected) == 0 && n == strlen(expected);
  free(dst);
  return ok;
}

int main(void) {
  // hex_to_int
  CHECK_EQ(hex_to_int('0'), 0);
  CHECK_EQ(hex_to_int('9'), 9);
  CHECK_EQ(hex_to_int('a'), 10);
  CHECK_EQ(hex_to_int('F'), 15);
  CHECK_EQ(hex_to_int('g'), -1);
  CHECK_EQ(hex_to_int('%'), -1);

  // url_decode: escapes, '+', a truncated escape is copied as typed.
  CHECK(url_is("a%20b+c", 16, "a b c"));
  CHECK(url_is("%41%62", 16, "Ab"));
  CHECK(url_is("50%", 16, "50%"));
  CHECK(url_is("%4", 16, "%4"));
  CHECK(url_is("", 1, ""));
  CHECK(url_fails("%zz", 16));
  CHECK(url_fails("%4g", 16));
  // Output exactly filling dst_len (with its terminator) fits; one more fails.
  CHECK(url_is("abc", 4, "abc"));
  CHECK(url_fails("abcd", 4));
  CHECK(url_is("%41%42%43", 4, "ABC"));
  CHECK(url_fails("%41%42%43%44", 4));

  // json_escape_string: escapes, control characters, truncation in bounds.
  CHECK(escaped_is("plain", 16, "plain"));
  CHECK(escaped_is("a\"b\\c", 16, "a\\\"b\\\\c"));
  CHECK(escaped_is("\n\r\t\b\f", 16, "\\n\\r\\t\\b\\f"));
  CHECK(escaped_is("\x01", 16, "\\u0001"));
  CHECK(escaped_is("abcdef", 4, "abc"));
  CHECK(escaped_is("ab\"", 4, "ab"));      // the escape does not fit whole
  CHECK(escaped_is("a\x01", 7, "a"));      // nor does \u0001
  CHECK(escaped_is("\x01", 7, "\\u0001"));
  CHECK(escaped_is(NULL, 4, ""));
  CHECK_EQ(json_escape_string("x", NULL, 0), 0);

  // starts_with_case_insensitive, as BOOL values use it.
  CHECK_EQ(starts_with_case_insensitive("yes", "YyTt"), 1);
  CHECK_EQ(starts_with_case_insensitive("True", "YyTt"), 1);
  CHECK_EQ(starts_with_case_insensitive("no", "YyTt"), 0);
  CHECK_EQ(starts_with_case_insensitive("", "YyTt"), 0);
  CHECK_EQ(starts_with_case_insensitive(NULL, "YyTt"), 0);

  // to_lowercase_copy truncates to dst_len.
  char lower[3];
  to_lowercase_copy("ABCDEF", lower, sizeof(lower));
  CHECK(strcmp(lower, "ab") == 0);

  // A valid request: every entry written, the settings saved.
  fresh_settings();
  CHECK_EQ(apply(encode("[{\"name\":\"WIFI_SSID\",\"type\":\"STRING\",\"value\":\"bench net\"},"
                        "{\"name\":\"MODE\",\"type\":\"INT\",\"value\":\"2\"},"
                        "{\"name\":\"HIDDEN_FLAG\",\"type\":\"BOOL\",\"value\":\"Yes\"}]")),
           SAVEPARAMS_SAVED);
  CHECK(strcmp(message, "") == 0);
  CHECK(strcmp(value("WIFI_SSID"), "bench net") == 0);
  CHECK(strcmp(value("MODE"), "2") == 0);
  CHECK(strcmp(value("HIDDEN_FLAG"), "true") == 0);
  CHECK(saved());

  // BOOL: anything not starting with y or t is false.
  CHECK_EQ(apply(encode("[{\"name\":\"HIDDEN_FLAG\",\"type\":\"bool\",\"value\":\"no\"}]")),
           SAVEPARAMS_SAVED);
  CHECK(strcmp(value("HIDDEN_FLAG"), "false") == 0);

  // USB_KB_LAYOUT is stored lowercase, at most 15 characters.
  CHECK_EQ(apply(encode("[{\"name\":\"USB_KB_LAYOUT\",\"type\":\"STRING\",\"value\":\"ES\"}]")),
           SAVEPARAMS_SAVED);
  CHECK(strcmp(value("USB_KB_LAYOUT"), "es") == 0);
  CHECK_EQ(apply(encode("[{\"name\":\"USB_KB_LAYOUT\",\"type\":\"STRING\","
                        "\"value\":\"ABCDEFGHIJKLMNOPQRST\"}]")),
           SAVEPARAMS_SAVED);
  CHECK(strcmp(value("USB_KB_LAYOUT"), "abcdefghijklmno") == 0);

  // MODE is stored as given, not clamped: 7 (the core firmware runs
  // configuration mode for a MODE it does not know), and 0 for a value that
  // is not a number (atoi), which is native mode on Croissant.
  CHECK_EQ(apply(encode("[{\"name\":\"MODE\",\"type\":\"INT\",\"value\":\"7\"}]")),
           SAVEPARAMS_SAVED);
  CHECK(strcmp(value("MODE"), "7") == 0);
  CHECK_EQ(apply(encode("[{\"name\":\"MODE\",\"type\":\"INT\",\"value\":\"abc\"}]")),
           SAVEPARAMS_SAVED);
  CHECK(strcmp(value("MODE"), "0") == 0);

  // Values: 95 characters kept, longer ones cut to 95.
  char json[512], longValue[201];
  memset(longValue, 'v', 200);
  longValue[200] = '\0';
  snprintf(json, sizeof(json),
           "[{\"name\":\"WIFI_SSID\",\"type\":\"STRING\",\"value\":\"%s\"}]", longValue);
  CHECK_EQ(apply(encode(json)), SAVEPARAMS_SAVED);
  CHECK_EQ(strlen(value("WIFI_SSID")), 95);

  // A key the firmware does not have is skipped; the rest is saved.
  CHECK_EQ(apply(encode("[{\"name\":\"NOT_A_KEY\",\"type\":\"STRING\",\"value\":\"x\"},"
                        "{\"name\":\"WIFI_SSID\",\"type\":\"STRING\",\"value\":\"after\"}]")),
           SAVEPARAMS_SAVED);
  CHECK(strcmp(value("WIFI_SSID"), "after") == 0);
  CHECK(settings_find_entry(&ctx, "NOT_A_KEY") == NULL);

  // An empty list saves the settings unchanged.
  CHECK_EQ(apply(encode("[]")), SAVEPARAMS_SAVED);

  // Rejected requests leave the flash as it was. Entries before the bad one
  // are already in the settings cache, unsaved.
  fresh_settings();
  CHECK_EQ(apply(encode("[{\"name\":\"WIFI_SSID\",\"type\":\"STRING\",\"value\":\"cached\"},"
                        "{\"name\":\"MODE\",\"type\":\"FLOAT\",\"value\":\"1\"}]")),
           SAVEPARAMS_BAD_REQUEST);
  CHECK(strcmp(message, "Invalid parameter type in JSON") == 0);
  CHECK(!saved());
  CHECK(strcmp(value("WIFI_SSID"), "cached") == 0);

  CHECK_EQ(apply(encode("[{\"name\":\"MODE\",\"type\":\"INT\"}]")), SAVEPARAMS_BAD_REQUEST);
  CHECK(strcmp(message, "Invalid parameter structure in JSON") == 0);
  CHECK_EQ(apply(encode("[{\"name\":\"MODE\",\"type\":\"INT\",\"value\":2}]")),
           SAVEPARAMS_BAD_REQUEST);
  CHECK(strcmp(message, "Invalid parameter structure in JSON") == 0);
  CHECK_EQ(apply(encode("{\"name\":\"MODE\",\"type\":\"INT\",\"value\":\"2\"}")),
           SAVEPARAMS_BAD_REQUEST);
  CHECK(strcmp(message, "Invalid parameter structure in JSON") == 0);
  CHECK_EQ(apply(encode("[{\"name\":")), SAVEPARAMS_BAD_REQUEST);
  CHECK(strcmp(message, "Error parsing JSON") == 0);
  CHECK_EQ(apply(encode("not json")), SAVEPARAMS_BAD_REQUEST);
  CHECK(strcmp(message, "Error parsing JSON") == 0);
  CHECK(!saved());

  // Base64 errors report mbedtls's code; -44 is an invalid character.
  CHECK_EQ(apply("@@@@"), SAVEPARAMS_BAD_BASE64);
  CHECK(strcmp(message, "Error decoding base64: -44") == 0);
  CHECK_EQ(apply("W3t9XQ+%3D%3D"), SAVEPARAMS_BAD_BASE64);  // '+' unescaped is a space
  CHECK_EQ(apply("W3t9XQ%3D%3D%3D"), SAVEPARAMS_BAD_BASE64);
  CHECK(!saved());

  // URL errors, including a parameter too long for the 4,096-byte buffer.
  CHECK_EQ(apply("W3t9%zz"), SAVEPARAMS_BAD_REQUEST);
  CHECK(strcmp(message, "Error URL-decoding parameter") == 0);
  char *huge = malloc(BUF + 1);
  memset(huge, 'A', BUF);
  huge[BUF] = '\0';
  CHECK_EQ(apply(huge), SAVEPARAMS_BAD_REQUEST);
  CHECK(strcmp(message, "Error URL-decoding parameter") == 0);
  // 4,095 base64 digits, the longest that fits: mbedtls decodes them even
  // though 4,095 is not a multiple of 4, and the zero bytes are not JSON.
  huge[BUF - 1] = '\0';
  CHECK_EQ(apply(huge), SAVEPARAMS_BAD_REQUEST);
  CHECK(strcmp(message, "Error parsing JSON") == 0);
  free(huge);
  CHECK(!saved());

  settings_deinit(&ctx);
  TEST_END();
}
