// units: booster/src/httpd_params.c booster/src/settings/settings.c booster/src/cjson/cJSON.c pico-sdk/lib/mbedtls/library/base64.c tests/host/shim/host_flash.c
/* Defect: saveparams_apply() terminates the base64 output with
 * outBuf[len] = '\0' before checking mbedtls_base64_decode()'s result.
 * A decode that exactly fills outBuf writes one byte past it, and when
 * outBuf is too small mbedtls sets len to the size it needed, so the write
 * lands further out. With the firmware's buffers (4,096 bytes each, so at
 * most 4,095 base64 digits, which decode to 3,071 bytes) neither can happen;
 * it bites as soon as the two sizes change. */
#include <stdlib.h>
#include <string.h>

#include "httpd_params.h"
#include "test.h"

int main(void) {
  SettingsContext ctx = {0};
  char url_buf[64], message[128];

  // "YWJj" decodes to the 3 bytes "abc": exactly fills a 3-byte outBuf.
  char *out = malloc(3);
  saveparams_apply(&ctx, "YWJj", url_buf, sizeof(url_buf), out, 3, message,
                   sizeof(message));
  free(out);
  CHECK(1);
  TEST_END();
}
