/**
 * File: httpd_params.c
 * Author: Diego Parrilla Santamaría
 * Date: November 2026
 * Copyright: 2024-2026 - GOODDATA LABS SL
 * Description: Decoding of the web console's requests: the text helpers the
 * pages use and what /saveparams.cgi does with its json parameter. Free of
 * lwIP, so the host tests build it as it is.
 */

#include "httpd_params.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cjson/cJSON.h"
#include "debug.h"
#include "gconfig.h"
#include "mbedtls/base64.h"

size_t json_escape_string(const char *src, char *dst, size_t dst_size) {
  if (dst_size == 0) {
    return 0;
  }
  if (src == NULL) {
    dst[0] = '\0';
    return 0;
  }

  size_t di = 0;
  for (size_t si = 0; src[si] != '\0'; si++) {
    const char *escaped = NULL;
    switch (src[si]) {
      case '\"':
        escaped = "\\\"";
        break;
      case '\\':
        escaped = "\\\\";
        break;
      case '\b':
        escaped = "\\b";
        break;
      case '\f':
        escaped = "\\f";
        break;
      case '\n':
        escaped = "\\n";
        break;
      case '\r':
        escaped = "\\r";
        break;
      case '\t':
        escaped = "\\t";
        break;
      default:
        break;
    }

    if (escaped != NULL) {
      size_t escaped_len = strlen(escaped);
      if (di + escaped_len >= dst_size) {
        break;
      }
      memcpy(dst + di, escaped, escaped_len);
      di += escaped_len;
      continue;
    }

    unsigned char c = (unsigned char)src[si];
    if (c < 0x20) {
      if (di + 6 >= dst_size) {
        break;
      }
      int written = snprintf(dst + di, dst_size - di, "\\u%04x", (unsigned)c);
      if (written <= 0 || (size_t)written >= (dst_size - di)) {
        break;
      }
      di += (size_t)written;
      continue;
    }

    if (di + 1 >= dst_size) {
      break;
    }
    dst[di++] = (char)c;
  }

  dst[di] = '\0';
  return di;
}

/**
 * @brief Check if the string starts with specified characters
 * (case-insensitive).
 *
 * @param str The string to check.
 * @param chars The characters to match (e.g., "YyTt").
 * @return 1 if the string starts with any of the specified characters;
 * otherwise, 0.
 */
int starts_with_case_insensitive(const char *str, const char *chars) {
  if (str == NULL || chars == NULL || str[0] == '\0') {
    return 0;
  }
  char first_char = tolower((unsigned char)str[0]);
  while (*chars) {
    if (first_char == tolower((unsigned char)*chars)) {
      return 1;
    }
    chars++;
  }
  return 0;
}

int hex_to_int(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
  if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
  return -1;
}

void to_lowercase_copy(const char *src, char *dst, size_t dst_len) {
  if (dst_len == 0) {
    return;
  }
  size_t i = 0;
  for (; src[i] != '\0' && i + 1 < dst_len; i++) {
    dst[i] = (char)tolower((unsigned char)src[i]);
  }
  dst[i] = '\0';
}

/**
 * @brief URL-decode a percent-encoded string.
 *
 * @param src Source string (null-terminated).
 * @param dst Destination buffer.
 * @param dst_len Length of destination buffer.
 * @return 0 on success, -1 on overflow or invalid hex.
 */
int url_decode(const char *src, char *dst, size_t dst_len) {
  size_t si = 0;
  size_t di = 0;
  while (src[si] != '\0') {
    if (di + 1 >= dst_len) {  // leave space for null terminator
      return -1;
    }
    if (src[si] == '%' && src[si + 1] && src[si + 2]) {
      int hi = hex_to_int(src[si + 1]);
      int lo = hex_to_int(src[si + 2]);
      if (hi < 0 || lo < 0) {
        return -1;
      }
      dst[di++] = (char)((hi << 4) | lo);
      si += 3;
    } else if (src[si] == '+') {
      dst[di++] = ' ';
      si++;
    } else {
      dst[di++] = src[si++];
    }
  }
  dst[di] = '\0';
  return 0;
}

saveparams_result_t saveparams_apply(SettingsContext *ctx, const char *param,
                                     char *urlBuf, size_t urlSize, char *outBuf,
                                     size_t outSize, char *message,
                                     size_t messageSize) {
  saveparams_result_t result = SAVEPARAMS_BAD_REQUEST;
  int requested_mode = -1;
  int ret;
  size_t len = 0;
  if (url_decode(param, urlBuf, urlSize) != 0) {
    DPRINTF("Error URL-decoding base64 param\n");
    snprintf(message, messageSize, "%s", "Error URL-decoding parameter");
    return SAVEPARAMS_BAD_REQUEST;
  }
  ret = mbedtls_base64_decode((unsigned char *)outBuf,  // destination
                              outSize,                  // dest size
                              &len,  // number of bytes decoded
                              (const unsigned char *)urlBuf, strlen(urlBuf));
  // Place a null terminator at the end of the string
  outBuf[len] = '\0';
  if (ret != 0) {
    DPRINTF("Error decoding base64: %d\n", ret);
    result = SAVEPARAMS_BAD_BASE64;
    snprintf(message, messageSize, "Error decoding base64: %d", ret);
  } else {
    DPRINTF("Decoded value: %s\n", outBuf);
    // Parse the JSON object
    bool valid_json = true;
    cJSON *root = cJSON_Parse(outBuf);
    if (root == NULL) {
      DPRINTF("Error parsing JSON\n");
      result = SAVEPARAMS_BAD_REQUEST;
      snprintf(message, messageSize, "Error parsing JSON");
      valid_json = false;
    } else {
      // Iterate over the JSON array
      cJSON *item = NULL;
      cJSON_ArrayForEach(item, root) {
        cJSON *name = cJSON_GetObjectItem(item, "name");
        cJSON *type = cJSON_GetObjectItem(item, "type");
        cJSON *value = cJSON_GetObjectItem(item, "value");

        // Print each parameter
        if (cJSON_IsString(name) && cJSON_IsString(type) &&
            cJSON_IsString(value)) {
          DPRINTF("Param Name: %s, Type: %s, Value: %s\n", name->valuestring,
                  type->valuestring, value->valuestring);
          if (strcasecmp(type->valuestring, "STRING") == 0) {
            // Lowercase USB KB layout to keep stored value consistent
            if (strcasecmp(name->valuestring, PARAM_USB_KB_LAYOUT) == 0) {
              char lower_buf[16];
              to_lowercase_copy(value->valuestring, lower_buf,
                                sizeof(lower_buf));
              settings_put_string(ctx, name->valuestring, lower_buf);
              DPRINTF("Setting %s to %s saved (normalized lowercase).\n",
                      name->valuestring, lower_buf);
            } else {
              settings_put_string(ctx, name->valuestring, value->valuestring);
              DPRINTF("Setting %s to %s saved.\n", name->valuestring,
                      value->valuestring);
            }
          } else if (strcasecmp(type->valuestring, "INT") == 0) {
            int int_value = atoi(value->valuestring);
            if (strcasecmp(name->valuestring, PARAM_MODE) == 0) {
              requested_mode = int_value;
            }
            settings_put_integer(ctx, name->valuestring, int_value);
            DPRINTF("Setting %s to %d saved.\n", name->valuestring, int_value);
          } else if (strcasecmp(type->valuestring, "BOOL") == 0) {
            // Check if the value starts with "Y", "y", "T", or "t"
            int bool_value =
                starts_with_case_insensitive(value->valuestring, "YyTt");
            settings_put_bool(ctx, name->valuestring, bool_value);
            DPRINTF("Setting %s to %s saved.\n", name->valuestring,
                    bool_value ? "true" : "false");
          } else {
            DPRINTF("Invalid parameter type in JSON\n");
            result = SAVEPARAMS_BAD_REQUEST;
            snprintf(message, messageSize, "Invalid parameter type in JSON");
            valid_json = false;
          }
        } else {
          DPRINTF("Invalid parameter structure in JSON\n");
          result = SAVEPARAMS_BAD_REQUEST;
          snprintf(message, messageSize, "Invalid parameter structure in JSON");
          valid_json = false;
        }
      }

      // Free the parsed JSON object
      cJSON_Delete(root);

      if (valid_json) {
        if (requested_mode == 1 || requested_mode == 2) {
          settings_put_integer(ctx, PARAM_MODE, requested_mode);
          DPRINTF("Setting %s to %d saved (selected mode).\n", PARAM_MODE,
                  requested_mode);
        }
        settings_save(ctx, true);
        DPRINTF("Settings saved\n");
        result = SAVEPARAMS_SAVED;
        snprintf(message, messageSize, "");
      }
    }
  }
  return result;
}
