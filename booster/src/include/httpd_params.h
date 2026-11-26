/**
 * File: httpd_params.h
 * Author: Diego Parrilla Santamaría
 * Date: November 2026
 * Copyright: 2024-2026 - GOODDATA LABS SL
 * Description: Decoding of the web console's requests (see httpd_params.c).
 */

#ifndef HTTPD_PARAMS_H
#define HTTPD_PARAMS_H

#include <stddef.h>

#include "settings.h"

typedef enum {
  SAVEPARAMS_SAVED = 0,    // Every entry written and the settings saved
  SAVEPARAMS_BAD_REQUEST,  // Nothing saved; message says why
  SAVEPARAMS_BAD_BASE64,   // Nothing saved; message has mbedtls's error
} saveparams_result_t;

size_t json_escape_string(const char *src, char *dst, size_t dst_size);
int starts_with_case_insensitive(const char *str, const char *chars);
int hex_to_int(char c);
void to_lowercase_copy(const char *src, char *dst, size_t dst_len);
int url_decode(const char *src, char *dst, size_t dst_len);

/**
 * @brief Apply /saveparams.cgi's json parameter to the settings.
 *
 * The parameter is URL-encoded base64 of a JSON array of
 * {"name", "type", "value"} entries, all strings, type STRING, INT or BOOL.
 * Each entry is written to ctx; when all are valid the settings are saved.
 * USB_KB_LAYOUT is stored lowercase. urlBuf and outBuf are the work buffers
 * for the URL-decoded and the base64-decoded text.
 *
 * @return SAVEPARAMS_SAVED with an empty message, or the error and its message.
 */
saveparams_result_t saveparams_apply(SettingsContext *ctx, const char *param,
                                     char *urlBuf, size_t urlSize, char *outBuf,
                                     size_t outSize, char *message,
                                     size_t messageSize);

#endif  // HTTPD_PARAMS_H
