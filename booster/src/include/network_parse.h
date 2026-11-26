/**
 * File: network_parse.h
 * Author: Diego Parrilla Santamaría
 * Date: November 2026
 * Copyright: 2024-2026 - GOODDATA LABS SL
 * Description: The Wi-Fi settings as the CYW43 driver takes them (see
 * network_parse.c).
 */

#ifndef NETWORK_PARSE_H
#define NETWORK_PARSE_H

#include <stdbool.h>
#include <stdint.h>

#define NETWORK_MAC_SIZE 6
#define MAX_BSSID_LENGTH 20

/**
 * @brief The CYW43 country code for a WIFI_COUNTRY value.
 *
 * Two letters from the driver's list, in any case; anything else selects
 * worldwide.
 *
 * @param code The setting's value.
 * @param validCountryStr Set to the code used, "XX" for worldwide.
 * @return The CYW43_COUNTRY() value.
 */
uint32_t network_getCountryCode(char *code, char **validCountryStr);

/**
 * @brief The CYW43_AUTH_* mode for a WIFI_AUTH number (0-8; others open).
 */
uint32_t network_getAuthPicoCode(uint16_t connectCode);

/**
 * @brief Parse a BSSID, xx:xx:xx:xx:xx:xx or xx-xx-xx-xx-xx-xx.
 *
 * @param value The text, for example a WIFI_SSID value.
 * @param outBssid The six bytes.
 * @param outBssidStr The BSSID lowercase, with ':'.
 * @return true when value is a BSSID.
 */
bool network_parseBssid(const char *value, uint8_t outBssid[NETWORK_MAC_SIZE],
                        char outBssidStr[MAX_BSSID_LENGTH]);

/**
 * @brief Converts a numeric WiFi authentication code into a descriptive string.
 *
 * Facilitates understanding of the authentication mode used.
 *
 * @param connect_code Numeric authentication code.
 * @return Pointer to a string with the full authentication type description.
 */
const char *network_getAuthTypeString(uint16_t connect_code);

/**
 * @brief Returns an abbreviated string for the WiFi authentication type.
 *
 * Useful when display area is limited.
 *
 * @param connect_code Numeric authentication code.
 * @return Pointer to a short string representing the authentication type.
 */
const char *network_getAuthTypeStringShort(uint16_t connect_code);

#endif  // NETWORK_PARSE_H
