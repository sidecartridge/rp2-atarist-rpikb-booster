/**
 * File: network_parse.c
 * Author: Diego Parrilla Santamaría
 * Date: November 2026
 * Copyright: 2024-2026 - GOODDATA LABS SL
 * Description: The Wi-Fi settings as the CYW43 driver takes them: country
 * code, BSSID and authentication mode. Free of lwIP and the driver's
 * functions, so the host tests build it as it is.
 */

#include "network_parse.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "constants.h"
#include "cyw43_country.h"
#include "cyw43_ll.h"

uint32_t network_getCountryCode(char *code, char **validCountryStr) {
  *validCountryStr = "XX";
  // empty configuration select worldwide
  if (strlen(code) == 0) {
    return CYW43_COUNTRY_WORLDWIDE;
  }

  if (strlen(code) != 2) {
    return CYW43_COUNTRY_WORLDWIDE;
  }

  // current supported country code
  // https://www.raspberrypi.com/documentation/pico-sdk/networking.html#CYW43_COUNTRY_
  // ISO-3166-alpha-2
  // XX select worldwide
  char *validCountryCode[] = {
      "XX", "AU", "AR", "AT", "BE", "BR", "CA", "CL", "CN", "CO", "CZ",
      "DK", "EE", "FI", "FR", "DE", "GR", "HK", "HU", "IS", "IN", "IL",
      "IT", "JP", "KE", "LV", "LI", "LT", "LU", "MY", "MT", "MX", "NL",
      "NZ", "NG", "NO", "PE", "PH", "PL", "PT", "SG", "SK", "SI", "ZA",
      "KR", "ES", "SE", "CH", "TW", "TH", "TR", "GB", "US"};

  char country[3] = {toupper(code[0]), toupper(code[1]), 0};
  for (int i = 0; i < (sizeof(validCountryCode) / sizeof(validCountryCode[0]));
       i++) {
    if (!strcmp(country, validCountryCode[i])) {
      *validCountryStr = validCountryCode[i];
      return CYW43_COUNTRY(country[0], country[1], 0);
    }
  }
  return CYW43_COUNTRY_WORLDWIDE;
}

// NOLINTBEGIN(readability-magic-numbers)
uint32_t network_getAuthPicoCode(uint16_t connectCode) {
  switch (connectCode) {
    case 0:
      return CYW43_AUTH_OPEN;
    case 1:
    case 2:
      return CYW43_AUTH_WPA_TKIP_PSK;
    case 3:
    case 4:
    case 5:
      return CYW43_AUTH_WPA2_AES_PSK;
    case 6:
    case 7:
    case 8:
      return CYW43_AUTH_WPA2_MIXED_PSK;
    default:
      return CYW43_AUTH_OPEN;
  }
}
// NOLINTEND(readability-magic-numbers)

bool network_parseBssid(const char *value, uint8_t outBssid[NETWORK_MAC_SIZE],
                        char outBssidStr[MAX_BSSID_LENGTH]) {
  if (value == NULL || outBssid == NULL || outBssidStr == NULL) {
    return false;
  }

  // Canonical BSSID format: xx:xx:xx:xx:xx:xx
  if (strlen(value) != 17) {
    return false;
  }

  char sep = value[2];
  if (sep != ':' && sep != '-') {
    return false;
  }

  for (int i = 0; i < NETWORK_MAC_SIZE; i++) {
    int idx = i * 3;
    if (!isxdigit((unsigned char)value[idx]) ||
        !isxdigit((unsigned char)value[idx + 1])) {
      return false;
    }
    if (i < NETWORK_MAC_SIZE - 1 && value[idx + 2] != sep) {
      return false;
    }

    char hexPair[3] = {value[idx], value[idx + 1], '\0'};
    outBssid[i] = (uint8_t)strtoul(hexPair, NULL, HEX_BASE);

    outBssidStr[idx] = (char)tolower((unsigned char)value[idx]);
    outBssidStr[idx + 1] = (char)tolower((unsigned char)value[idx + 1]);
    if (i < NETWORK_MAC_SIZE - 1) {
      outBssidStr[idx + 2] = ':';
    }
  }
  outBssidStr[17] = '\0';
  return true;
}

// NOLINTBEGIN(readability-magic-numbers)
const char *network_getAuthTypeString(uint16_t connectCode) {
  switch (connectCode) {
    case 0:
      return "OPEN";
    case 1:
    case 2:
      return "WPA_TKIP_PSK";
    case 3:
    case 4:
    case 5:
      return "WPA2_AES_PSK";
    case 6:
    case 7:
    case 8:
      return "WPA2_MIXED_PSK";
    default:
      return "OPEN";
  }
}
// NOLINTEND(readability-magic-numbers)

// NOLINTBEGIN(readability-magic-numbers)
const char *network_getAuthTypeStringShort(uint16_t connectCode) {
  switch (connectCode) {
    case 1:
    case 2:
      return "WPA";
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
      return "WPA2";
    default:
      return "OPEN";
  }
}
// NOLINTEND(readability-magic-numbers)
