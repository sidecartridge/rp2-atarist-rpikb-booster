// units: booster/src/network_parse.c
/* The Wi-Fi settings Booster hands to the CYW43 driver: the country code,
 * the BSSID a WIFI_SSID value may hold, and the WIFI_AUTH number, checked
 * against the driver's own constants. */
#include <string.h>

#include "cyw43_country.h"
#include "cyw43_ll.h"
#include "network_parse.h"
#include "test.h"

static int country_is(const char *code, uint32_t expected, const char *valid) {
  char buf[8];
  char *got = NULL;
  snprintf(buf, sizeof(buf), "%s", code);
  return network_getCountryCode(buf, &got) == expected && strcmp(got, valid) == 0;
}

static int bssid_is(const char *text, const char *canonical, const uint8_t *bytes) {
  uint8_t mac[NETWORK_MAC_SIZE];
  char str[MAX_BSSID_LENGTH];
  return network_parseBssid(text, mac, str) && strcmp(str, canonical) == 0 &&
         memcmp(mac, bytes, NETWORK_MAC_SIZE) == 0;
}

static int not_bssid(const char *text) {
  uint8_t mac[NETWORK_MAC_SIZE];
  char str[MAX_BSSID_LENGTH];
  return !network_parseBssid(text, mac, str);
}

int main(void) {
  // Country: two letters in the driver's list, any case; anything else is
  // worldwide (XX).
  CHECK(country_is("GB", CYW43_COUNTRY('G', 'B', 0), "GB"));
  CHECK(country_is("es", CYW43_COUNTRY('E', 'S', 0), "ES"));
  CHECK(country_is("uS", CYW43_COUNTRY('U', 'S', 0), "US"));
  CHECK(country_is("XX", CYW43_COUNTRY_WORLDWIDE, "XX"));
  CHECK(country_is("", CYW43_COUNTRY_WORLDWIDE, "XX"));
  CHECK(country_is("G", CYW43_COUNTRY_WORLDWIDE, "XX"));
  CHECK(country_is("GBR", CYW43_COUNTRY_WORLDWIDE, "XX"));
  CHECK(country_is("ZZ", CYW43_COUNTRY_WORLDWIDE, "XX"));
  CHECK(country_is("1A", CYW43_COUNTRY_WORLDWIDE, "XX"));

  // A WIFI_SSID value is a BSSID when it is six hex pairs with one separator
  // (':' or '-'), stored lowercase with ':'.
  static const uint8_t mac[NETWORK_MAC_SIZE] = {0xAA, 0xBB, 0xCC, 0x01, 0x23, 0xEF};
  CHECK(bssid_is("aa:bb:cc:01:23:ef", "aa:bb:cc:01:23:ef", mac));
  CHECK(bssid_is("AA:BB:CC:01:23:EF", "aa:bb:cc:01:23:ef", mac));
  CHECK(bssid_is("AA-BB-CC-01-23-EF", "aa:bb:cc:01:23:ef", mac));
  CHECK(not_bssid("AA-BB:CC-01-23-EF"));  // mixed separators
  CHECK(not_bssid("AA:BB:CC:01:23:E"));   // too short
  CHECK(not_bssid("AA:BB:CC:01:23:EF:"));  // too long
  CHECK(not_bssid("AA:BB:CC:01:23:EG"));  // not hex
  CHECK(not_bssid("AA.BB.CC.01.23.EF"));
  CHECK(not_bssid("my home network!!"));  // 17 characters, an SSID
  CHECK(not_bssid(""));
  CHECK(not_bssid(NULL));

  // WIFI_AUTH: 0 open, 1-2 WPA, 3-5 WPA2 AES, 6-8 WPA2 mixed, others open.
  CHECK_EQ(network_getAuthPicoCode(0), CYW43_AUTH_OPEN);
  CHECK_EQ(network_getAuthPicoCode(1), CYW43_AUTH_WPA_TKIP_PSK);
  CHECK_EQ(network_getAuthPicoCode(2), CYW43_AUTH_WPA_TKIP_PSK);
  CHECK_EQ(network_getAuthPicoCode(3), CYW43_AUTH_WPA2_AES_PSK);
  CHECK_EQ(network_getAuthPicoCode(5), CYW43_AUTH_WPA2_AES_PSK);
  CHECK_EQ(network_getAuthPicoCode(6), CYW43_AUTH_WPA2_MIXED_PSK);
  CHECK_EQ(network_getAuthPicoCode(8), CYW43_AUTH_WPA2_MIXED_PSK);
  CHECK_EQ(network_getAuthPicoCode(9), CYW43_AUTH_OPEN);
  CHECK_EQ(network_getAuthPicoCode(65535), CYW43_AUTH_OPEN);

  // The names the web pages show for the same numbers.
  CHECK(strcmp(network_getAuthTypeString(0), "OPEN") == 0);
  CHECK(strcmp(network_getAuthTypeString(2), "WPA_TKIP_PSK") == 0);
  CHECK(strcmp(network_getAuthTypeString(5), "WPA2_AES_PSK") == 0);
  CHECK(strcmp(network_getAuthTypeString(8), "WPA2_MIXED_PSK") == 0);
  CHECK(strcmp(network_getAuthTypeString(9), "OPEN") == 0);
  CHECK(strcmp(network_getAuthTypeStringShort(0), "OPEN") == 0);
  CHECK(strcmp(network_getAuthTypeStringShort(1), "WPA") == 0);
  CHECK(strcmp(network_getAuthTypeStringShort(3), "WPA2") == 0);
  CHECK(strcmp(network_getAuthTypeStringShort(8), "WPA2") == 0);
  CHECK(strcmp(network_getAuthTypeStringShort(9), "OPEN") == 0);

  TEST_END();
}
