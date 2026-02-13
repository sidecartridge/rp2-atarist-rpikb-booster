/**
 * File: mngr_httpd.c
 * Author: Diego Parrilla Santamaría
 * Date: December 2024
 * Copyright: 2024-2025 - GOODDATA LABS SL
 * Description: HTTPD server functions for manager httpd
 */

#include "mngr_httpd.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "btstack.h"
#include "btstack_util.h"
#include "cjson/cJSON.h"
#include "constants.h"
#include "debug.h"
#include "gconfig.h"
#include "include/btloop.h"
#include "lwip/apps/httpd.h"
#include "lwip/err.h"
#include "lwip/pbuf.h"
#include "mbedtls/base64.h"
#include "network.h"
#include "version.h"

// Basic stubs for LWIP_HTTPD_SUPPORT_POST to satisfy linker; POST bodies are
// ignored.
err_t httpd_post_begin(void *connection, const char *uri,
                       const char *http_request, u16_t http_request_len,
                       int content_len, char *response_uri,
                       u16_t response_uri_len, u8_t *post_auto_wnd) {
  (void)connection;
  (void)uri;
  (void)http_request;
  (void)http_request_len;
  (void)content_len;
  if (post_auto_wnd != NULL) {
    *post_auto_wnd = 1;  // let httpd handle windowing
  }
  if (response_uri != NULL && response_uri_len > 0) {
    response_uri[0] = '\0';  // default response
  }
  return ERR_OK;
}

err_t httpd_post_receive_data(void *connection, struct pbuf *p) {
  (void)connection;
  if (p != NULL) {
    pbuf_free(p);  // free received data
  }
  return ERR_OK;
}

void httpd_post_finished(void *connection, char *response_uri,
                         u16_t response_uri_len) {
  (void)connection;
  if (response_uri != NULL && response_uri_len > 0) {
    response_uri[0] = '\0';
  }
}

#define WIFI_PASS_BUFSIZE 64
static char *ssid = NULL;
static char *pass = NULL;
static int auth = -1;
static void *current_connection;
static void *valid_connection;
static mngr_httpd_response_status_t response_status = MNGR_HTTPD_RESPONSE_OK;
static char httpd_response_message[128] = {0};
static char httpd_json_payload[1024] = "[]";

static bool parse_addr_from_setting_value(const char *value, bd_addr_t addr) {
  if (value == NULL || value[0] == '\0') {
    return false;
  }
  char addr_buf[32];
  size_t len = 0;
  while (value[len] != '\0' && value[len] != '#' && len < (sizeof(addr_buf) - 1)) {
    addr_buf[len] = value[len];
    len++;
  }
  addr_buf[len] = '\0';
  if (len == 0) {
    return false;
  }
  return sscanf_bd_addr(addr_buf, addr) == 1;
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
static int starts_with_case_insensitive(const char *str, const char *chars) {
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

static int hex_to_int(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
  if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
  return -1;
}

static void to_lowercase_copy(const char *src, char *dst, size_t dst_len) {
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
static int url_decode(const char *src, char *dst, size_t dst_len) {
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

static char *get_status_message(mngr_httpd_response_status_t status,
                                const char *detail) {
  // Concatenate the detail
  char *message = (char *)malloc(128);
  if (message == NULL) {
    return NULL;
  }
  switch (status) {
    case MNGR_HTTPD_RESPONSE_OK:
      snprintf(message, 128, "OK: %s", detail);
      break;
    case MNGR_HTTPD_RESPONSE_BAD_REQUEST:
      snprintf(message, 128, "Bad Request: %s", detail);
      break;
    case MNGR_HTTPD_RESPONSE_NOT_FOUND:
      snprintf(message, 128, "Not Found: %s", detail);
      break;
    case MNGR_HTTPD_RESPONSE_INTERNAL_SERVER_ERROR:
      snprintf(message, 128, "Internal Server Error: %s", detail);
      break;
    default:
      snprintf(message, 128, "Unknown status: %s", detail);
      break;
  }
  return message;
}

/**
 * @brief Array of SSI tags for the HTTP server.
 *
 * This array contains the SSI tags used by the HTTP server to dynamically
 * insert content into web pages. Each tag corresponds to a specific piece of
 * information that can be updated or retrieved from the server.
 */
static const char *ssi_tags[] = {
    // Max size of SSI tag is 8 chars
    "HOMEPAGE",  // 0 - Redirect to the homepage
    "SSID",      // 1 - SSID
    "IPADDR",    // 2 - IP address
    "JSONPLD",   // 3 - JSON payload
    "TITLEHDR",  // 4 - Title header
    "RSPSTS",    // 5 - Response status
    "RSPMSG",    // 6 - Response message
    "MODE",      // 7 - IKBD emulation mode
    "JUSB",      // 8 - Joystick over USB enabled
    "JPORT",     // 9 - Joystick USB port
    "MORIG",     // 10 - Original mouse passthrough
    "MSPEED",    // 11 - Mouse speed
    "KBLANG",    // 12 - USB keyboard layout
    "BTKBL",     // 13 - BT keyboard layout
    "CTARGET",   // 14 - Computer target mask
    "BTGSHT",    // 15 - BT gamepad auto-shoot speed
    "JASHT",     // 16 - USB joystick auto-shoot speed
    "WFIMODE",   // 17 - WiFi mode (0=AP,1=STA)
    "WFIHOST",   // 18 - WiFi hostname
    "WFISSID",   // 19 - WiFi SSID
    "WFIPASS",   // 20 - WiFi password
    "WFIAUTH",   // 21 - WiFi auth mode
    "WDFHOST",   // 22 - WiFi default AP hostname
    "WDFPASS",   // 23 - WiFi default AP password
    "WDFAUTH",   // 24 - WiFi default AP auth mode
};

/**
 * @brief
 *
 * @param iIndex The index of the CGI handler.
 * @param iNumParams The number of parameters passed to the CGI handler.
 * @param pcParam An array of parameter names.
 * @param pcValue An array of parameter values.
 * @return The URL of the page to redirect to after the floppy disk image is
 * selected.
 */
static const char *cgi_test(int iIndex, int iNumParams, char *pcParam[],
                            char *pcValue[]) {
  DPRINTF("TEST CGI handler called with index %d\n", iIndex);
  return "/test.shtml";
}

/**
 * @brief Update the given params with the new values
 *
 *
 * @param iIndex The index of the CGI handler.
 * @param iNumParams The number of parameters passed to the CGI handler.
 * @param pcParam An array of parameter names.
 * @param pcValue An array of parameter values.
 * @return The URL of the page to redirect to wait for the download process
 */
const char *cgi_saveparams(int iIndex, int iNumParams, char *pcParam[],
                          char *pcValue[]) {
  DPRINTF("cgi_saveparams called with index %d\n", iIndex);
  int requested_mode = -1;
  for (size_t i = 0; i < iNumParams; i++) {
    /* check if parameter is "json" */
    if (strcmp(pcParam[i], "json") == 0) {
      DPRINTF("JSON encoded value: %s\n", pcValue[i]);
      int ret;
      size_t len = 0;
      char url_decoded_param[4096];
      char output_buffer[4096];
      if (url_decode(pcValue[i], url_decoded_param,
                     sizeof(url_decoded_param)) != 0) {
        DPRINTF("Error URL-decoding base64 param\n");
        response_status = MNGR_HTTPD_RESPONSE_BAD_REQUEST;
        snprintf(httpd_response_message, sizeof(httpd_response_message), "%s",
                 "Error URL-decoding parameter");
        return "/response.shtml";
      }
      ret = mbedtls_base64_decode(
          (unsigned char *)output_buffer,  // destination
          sizeof(output_buffer),           // dest size
          &len,                            // number of bytes decoded
          (const unsigned char *)url_decoded_param, strlen(url_decoded_param));
      // Place a null terminator at the end of the string
      output_buffer[len] = '\0';
      if (ret != 0) {
        DPRINTF("Error decoding base64: %d\n", ret);
        response_status = MNGR_HTTPD_RESPONSE_BAD_REQUEST;
        char detail[64] = {0};
        snprintf(detail, sizeof(detail), "Error decoding base64: %d", ret);
        snprintf(httpd_response_message, sizeof(httpd_response_message), "%s",
                 get_status_message(response_status, detail));
      } else {
        DPRINTF("Decoded value: %s\n", output_buffer);
        // Parse the JSON object
        bool valid_json = true;
        cJSON *root = cJSON_Parse(output_buffer);
        if (root == NULL) {
          DPRINTF("Error parsing JSON\n");
          response_status = MNGR_HTTPD_RESPONSE_BAD_REQUEST;
          snprintf(httpd_response_message, sizeof(httpd_response_message),
                   "Error parsing JSON");
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
              DPRINTF("Param Name: %s, Type: %s, Value: %s\n",
                      name->valuestring, type->valuestring, value->valuestring);
              if (strcasecmp(type->valuestring, "STRING") == 0) {
                // Lowercase USB KB layout to keep stored value consistent
                if (strcasecmp(name->valuestring, PARAM_USB_KB_LAYOUT) == 0) {
                  char lower_buf[16];
                  to_lowercase_copy(value->valuestring, lower_buf,
                                    sizeof(lower_buf));
                  settings_put_string(gconfig_getContext(), name->valuestring,
                                      lower_buf);
                  DPRINTF("Setting %s to %s saved (normalized lowercase).\n",
                          name->valuestring, lower_buf);
                } else {
                  settings_put_string(gconfig_getContext(), name->valuestring,
                                      value->valuestring);
                  DPRINTF("Setting %s to %s saved.\n", name->valuestring,
                          value->valuestring);
                }
              } else if (strcasecmp(type->valuestring, "INT") == 0) {
                int int_value = atoi(value->valuestring);
                if (strcasecmp(name->valuestring, PARAM_MODE) == 0) {
                  requested_mode = int_value;
                }
                settings_put_integer(gconfig_getContext(), name->valuestring,
                                     int_value);
                DPRINTF("Setting %s to %d saved.\n", name->valuestring,
                        int_value);
              } else if (strcasecmp(type->valuestring, "BOOL") == 0) {
                // Check if the value starts with "Y", "y", "T", or "t"
                int bool_value =
                    starts_with_case_insensitive(value->valuestring, "YyTt");
                settings_put_bool(gconfig_getContext(), name->valuestring,
                                  bool_value);
                DPRINTF("Setting %s to %s saved.\n", name->valuestring,
                        bool_value ? "true" : "false");
              } else {
                DPRINTF("Invalid parameter type in JSON\n");
                response_status = MNGR_HTTPD_RESPONSE_BAD_REQUEST;
                snprintf(httpd_response_message, sizeof(httpd_response_message),
                         "Invalid parameter type in JSON");
                valid_json = false;
              }
            } else {
              DPRINTF("Invalid parameter structure in JSON\n");
              response_status = MNGR_HTTPD_RESPONSE_BAD_REQUEST;
              snprintf(httpd_response_message, sizeof(httpd_response_message),
                       "Invalid parameter structure in JSON");
              valid_json = false;
            }
          }

          // Free the parsed JSON object
          cJSON_Delete(root);

          if (valid_json) {
            if (requested_mode == 1 || requested_mode == 2) {
              settings_put_integer(gconfig_getContext(), PARAM_MODE,
                                   requested_mode);
              DPRINTF("Setting %s to %d saved (selected mode).\n", PARAM_MODE,
                      requested_mode);
            }
            settings_save(gconfig_getContext(), true);
            DPRINTF("Settings saved\n");
            response_status = MNGR_HTTPD_RESPONSE_OK;
            snprintf(httpd_response_message, sizeof(httpd_response_message),
                     "");
          }
        }
      }
      return "/response.shtml";
    }
  }

  // If no "json" parameter is found
  response_status = MNGR_HTTPD_RESPONSE_BAD_REQUEST;
  snprintf(httpd_response_message, sizeof(httpd_response_message),
           "Missing 'json' parameter");
  return "/response.shtml";
}

const char *cgi_btlist(int iIndex, int iNumParams, char *pcParam[],
                       char *pcValue[]) {
  (void)iIndex;
  (void)iNumParams;
  (void)pcParam;
  (void)pcValue;

  const bt_device_info_t *devices = NULL;
  size_t count = 0;
  btloop_get_devices(&devices, &count);

  memset(httpd_json_payload, 0, sizeof(httpd_json_payload));

  int offset = snprintf(httpd_json_payload, sizeof(httpd_json_payload),
                        "{\"devices\":[");
  for (size_t i = 0; i < count && offset < (int)sizeof(httpd_json_payload) - 1;
       ++i) {
    int written = snprintf(
        httpd_json_payload + offset,
        sizeof(httpd_json_payload) - (size_t)offset,
        "%s{\"address\":\"%s\",\"name\":\"%s\",\"type\":\"%s\"}",
        (i > 0) ? "," : "", devices[i].address, devices[i].name,
        devices[i].type);
    if (written < 0) {
      break;
    }
    offset += written;
  }

  if (offset < (int)sizeof(httpd_json_payload) - 1) {
    snprintf(httpd_json_payload + offset,
             sizeof(httpd_json_payload) - (size_t)offset, "]}");
  } else {
    httpd_json_payload[sizeof(httpd_json_payload) - 1] = '\0';
  }

  response_status = MNGR_HTTPD_RESPONSE_OK;
  httpd_response_message[0] = '\0';
  DPRINTF("Bluetooth device list requested via CGI.\n");
  return "/json.shtml";
}

const char *cgi_btstart(int iIndex, int iNumParams, char *pcParam[],
                        char *pcValue[]) {
  (void)iIndex;
  (void)iNumParams;
  (void)pcParam;
  (void)pcValue;
  btloop_enable();
  response_status = MNGR_HTTPD_RESPONSE_OK;
  snprintf(httpd_response_message, sizeof(httpd_response_message),
           "Bluetooth loop started");
  DPRINTF("Bluetooth loop started via CGI.\n");
  return "/response.shtml";
}

const char *cgi_btstop(int iIndex, int iNumParams, char *pcParam[],
                       char *pcValue[]) {
  (void)iIndex;
  (void)iNumParams;
  (void)pcParam;
  (void)pcValue;
  btloop_disable();
  response_status = MNGR_HTTPD_RESPONSE_OK;
  snprintf(httpd_response_message, sizeof(httpd_response_message),
           "Bluetooth loop stopped");
  DPRINTF("Bluetooth loop stopped via CGI.\n");
  return "/response.shtml";
}

const char *cgi_btpairings(int iIndex, int iNumParams, char *pcParam[],
                           char *pcValue[]) {
  (void)iIndex;
  (void)iNumParams;
  (void)pcParam;
  (void)pcValue;

  SettingsConfigEntry *kb =
      settings_find_entry(gconfig_getContext(), PARAM_BT_KEYBOARD);
  SettingsConfigEntry *mouse =
      settings_find_entry(gconfig_getContext(), PARAM_BT_MOUSE);
  SettingsConfigEntry *gp =
      settings_find_entry(gconfig_getContext(), PARAM_BT_GAMEPAD);

  const char *kb_val = (kb && kb->value) ? kb->value : "";
  const char *ms_val = (mouse && mouse->value) ? mouse->value : "";
  const char *gp_val = (gp && gp->value) ? gp->value : "";

  char kb_addr[32] = {0}, kb_name[96] = {0};
  char ms_addr[32] = {0}, ms_name[96] = {0};
  char gp_addr[32] = {0}, gp_name[96] = {0};

  if (kb_val[0]) {
    const char *sep = strchr(kb_val, '#');
    if (sep) {
      snprintf(kb_addr, sizeof(kb_addr), "%.*s", (int)(sep - kb_val), kb_val);
      snprintf(kb_name, sizeof(kb_name), "%s", sep + 1);
    } else {
      snprintf(kb_addr, sizeof(kb_addr), "%s", kb_val);
    }
  }

  if (ms_val[0]) {
    const char *sep = strchr(ms_val, '#');
    if (sep) {
      snprintf(ms_addr, sizeof(ms_addr), "%.*s", (int)(sep - ms_val), ms_val);
      snprintf(ms_name, sizeof(ms_name), "%s", sep + 1);
    } else {
      snprintf(ms_addr, sizeof(ms_addr), "%s", ms_val);
    }
  }

  if (gp_val[0]) {
    const char *sep = strchr(gp_val, '#');
    if (sep) {
      snprintf(gp_addr, sizeof(gp_addr), "%.*s", (int)(sep - gp_val), gp_val);
      snprintf(gp_name, sizeof(gp_name), "%s", sep + 1);
    } else {
      snprintf(gp_addr, sizeof(gp_addr), "%s", gp_val);
    }
  }

  memset(httpd_json_payload, 0, sizeof(httpd_json_payload));
  snprintf(httpd_json_payload, sizeof(httpd_json_payload),
           "{\"keyboard\":{\"address\":\"%s\",\"name\":\"%s\"},"
           "\"mouse\":{\"address\":\"%s\",\"name\":\"%s\"},"
           "\"gamepad\":{\"address\":\"%s\",\"name\":\"%s\"}}",
           kb_addr, kb_name, ms_addr, ms_name, gp_addr, gp_name);

  response_status = MNGR_HTTPD_RESPONSE_OK;
  httpd_response_message[0] = '\0';
  return "/json.shtml";
}

const char *cgi_btclean(int iIndex, int iNumParams, char *pcParam[],
                        char *pcValue[]) {
  (void)iIndex;
  (void)iNumParams;
  (void)pcParam;
  (void)pcValue;
  btloop_clear_pairings();
  response_status = MNGR_HTTPD_RESPONSE_OK;
  snprintf(httpd_response_message, sizeof(httpd_response_message),
           "Bluetooth pairings cleared");
  DPRINTF("Bluetooth pairings cleared via CGI.\n");
  return "/response.shtml";
}

const char *cgi_btunpair(int iIndex, int iNumParams, char *pcParam[],
                         char *pcValue[]) {
  (void)iIndex;
  const char *type = NULL;
  for (int i = 0; i < iNumParams; i++) {
    if (strcmp(pcParam[i], "type") == 0) {
      type = pcValue[i];
      break;
    }
  }

  const char *param_key = NULL;
  const char *label = NULL;
  if (type != NULL) {
    if (strcmp(type, "keyboard") == 0) {
      param_key = PARAM_BT_KEYBOARD;
      label = "Keyboard";
    } else if (strcmp(type, "mouse") == 0) {
      param_key = PARAM_BT_MOUSE;
      label = "Mouse";
    } else if (strcmp(type, "gamepad") == 0) {
      param_key = PARAM_BT_GAMEPAD;
      label = "Gamepad";
    }
  }

  if (param_key == NULL) {
    response_status = MNGR_HTTPD_RESPONSE_BAD_REQUEST;
    snprintf(httpd_response_message, sizeof(httpd_response_message),
             "Invalid device type");
    return "/response.shtml";
  }

  SettingsConfigEntry *entry =
      settings_find_entry(gconfig_getContext(), param_key);
  bd_addr_t addr;
  bool have_addr =
      (entry != NULL && parse_addr_from_setting_value(entry->value, addr));

  // Keep BT stack lists clean even on single-unpair operation.
  btloop_clear_bt_lists();

  if (have_addr) {
#ifdef ENABLE_CLASSIC
    gap_drop_link_key_for_bd_addr(addr);
#endif
#ifdef ENABLE_BLE
    gap_delete_bonding(BD_ADDR_TYPE_LE_PUBLIC, addr);
    gap_delete_bonding(BD_ADDR_TYPE_LE_RANDOM, addr);
#endif
  }

  settings_put_string(gconfig_getContext(), param_key, "");
  settings_save(gconfig_getContext(), true);

  response_status = MNGR_HTTPD_RESPONSE_OK;
  snprintf(httpd_response_message, sizeof(httpd_response_message),
           "%s pairing cleared", label);
  return "/response.shtml";
}

/**
 * @brief Array of CGI handlers for floppy select and eject operations.
 *
 * This array contains the mappings between the CGI paths and the corresponding
 * handler functions for selecting and ejecting floppy disk images for drive A
 * and drive B.
 */
static const tCGI cgi_handlers[] = {{"/test.cgi", cgi_test},
                                    {"/saveparams.cgi", cgi_saveparams},
                                    {"/btlist.cgi", cgi_btlist},
                                    {"/btstart.cgi", cgi_btstart},
                                    {"/btstop.cgi", cgi_btstop},
                                    {"/btpairings.cgi", cgi_btpairings},
                                    {"/btclean.cgi", cgi_btclean},
                                    {"/btunpair.cgi", cgi_btunpair}};
/**
 * @brief Initializes the HTTP server with optional SSI tags, CGI handlers, and
 * an SSI handler function.
 *
 * This function initializes the HTTP server and sets up the provided Server
 * Side Include (SSI) tags, Common Gateway Interface (CGI) handlers, and SSI
 * handler function. It first calls the httpd_init() function to initialize the
 * HTTP server.
 *
 * The filesystem for the HTTP server is in the 'fs' directory in the project
 * root.
 *
 * @param ssi_tags An array of strings representing the SSI tags to be used in
 * the server-side includes.
 * @param num_tags The number of SSI tags in the ssi_tags array.
 * @param ssi_handler_func A pointer to the function that handles SSI tags.
 * @param cgi_handlers An array of tCGI structures representing the CGI handlers
 * to be used.
 * @param num_cgi_handlers The number of CGI handlers in the cgi_handlers array.
 */
static void httpd_server_init(const char *ssi_tags[], size_t num_tags,
                              tSSIHandler ssi_handler_func,
                              const tCGI *cgi_handlers,
                              size_t num_cgi_handlers) {
  httpd_init();

  // SSI Initialization
  if (num_tags > 0) {
    for (size_t i = 0; i < num_tags; i++) {
      LWIP_ASSERT("tag too long for LWIP_HTTPD_MAX_TAG_NAME_LEN",
                  strlen(ssi_tags[i]) <= LWIP_HTTPD_MAX_TAG_NAME_LEN);
    }
    http_set_ssi_handler(ssi_handler_func, ssi_tags, num_tags);
  } else {
    DPRINTF("No SSI tags defined.\n");
  }

  // CGI Initialization
  if (num_cgi_handlers > 0) {
    http_set_cgi_handlers(cgi_handlers, num_cgi_handlers);
  } else {
    DPRINTF("No CGI handlers defined.\n");
  }

  DPRINTF("HTTP server initialized.\n");
}

/**
 * @brief Server Side Include (SSI) handler for the HTTPD server.
 *
 * This function is called when the server needs to dynamically insert content
 * into web pages using SSI tags. It handles different SSI tags and generates
 * the corresponding content to be inserted into the web page.
 *
 * @param iIndex The index of the SSI handler.
 * @param pcInsert A pointer to the buffer where the generated content should be
 * inserted.
 * @param iInsertLen The length of the buffer.
 * @param current_tag_part The current part of the SSI tag being processed (used
 * for multipart SSI tags).
 * @param next_tag_part A pointer to the next part of the SSI tag to be
 * processed (used for multipart SSI tags).
 * @return The length of the generated content.
 */
static u16_t ssi_handler(int iIndex, char *pcInsert, int iInsertLen
#if LWIP_HTTPD_SSI_MULTIPART
                         ,
                         u16_t current_tag_part, u16_t *next_tag_part
#endif /* LWIP_HTTPD_SSI_MULTIPART */
) {
  // DPRINTF("SSI handler called with index %d\n", iIndex);
  size_t printed;
  switch (iIndex) {
    case 0: /* "HOMEPAGE" */
      // Always to the first step of the configuration
      printed = snprintf(
          pcInsert, iInsertLen, "%s",
          "<meta http-equiv='refresh' content='0;url=/mngr_home.shtml'>");
      break;
    case 1: /* "SSID" */
    {
      char *ssid =
          settings_find_entry(gconfig_getContext(), PARAM_WIFI_SSID)->value;
      if (ssid != NULL) {
        printed = snprintf(pcInsert, iInsertLen, "%s", ssid);
      } else {
        printed =
            snprintf(pcInsert, iInsertLen,
                     "<span class=\"text-error\">No network selected</span>");
      }
      break;
    }
    case 2: /* IPADDR */
    {
      ip_addr_t ipaddr = network_getCurrentIp();
      if (&ipaddr != NULL) {
        printed = snprintf(pcInsert, iInsertLen, "%s", ip4addr_ntoa(&ipaddr));
      } else {
        printed = snprintf(pcInsert, iInsertLen,
                           "<span class=\"text-error\">No IP address</span>");
      }
      break;
    }
    case 3: /* JSONPLD */
    {
      // DPRINTF("SSI JSONPLD handler called with index %d\n", iIndex);
      int chunk_size = 128;
      /* The offset into json based on current tag part */
      size_t offset = current_tag_part * chunk_size;
      size_t json_len = strlen(httpd_json_payload);

      /* If offset is beyond the end, we have no more data */
      if (offset >= json_len) {
        /* No more data, so no next part */
        printed = 0;
        break;
      }

      /* Calculate how many bytes remain from offset */
      size_t remain = json_len - offset;
      /* We want to send up to chunk_size bytes per part, or what's left if
       * <chunk_size */
      size_t chunk_len = (remain < chunk_size) ? remain : chunk_size;

      /* Also ensure we don't exceed iInsertLen - 1, to leave room for '\0' */
      if (chunk_len > (size_t)(iInsertLen - 1)) {
        chunk_len = iInsertLen - 1;
      }

      /* Copy that chunk into pcInsert */
      memcpy(pcInsert, &httpd_json_payload[offset], chunk_len);
      pcInsert[chunk_len] = '\0'; /* null-terminate */

      printed = (u16_t)chunk_len;

      /* If there's more data after this chunk, increment next_tag_part */
      if ((offset + chunk_len) < json_len) {
        *next_tag_part = current_tag_part + 1;
      }
      break;
    }
    case 4: /* TITLEHDR */
    {
#if _DEBUG == 0
      printed = snprintf(pcInsert, iInsertLen, "%s (%s)", BOOSTER_TITLE,
                         RELEASE_VERSION);
#else
      printed = snprintf(pcInsert, iInsertLen, "%s (%s-%s)", BOOSTER_TITLE,
                         RELEASE_VERSION, RELEASE_DATE);
#endif
      break;
    }
    case 5: /* RSPSTS */
    {
      printed = snprintf(pcInsert, iInsertLen, "%d", response_status);
      break;
    }
    case 6: /* RSPMSG */
    {
      printed = snprintf(pcInsert, iInsertLen, "%s", httpd_response_message);
      break;
    }
    case 7: /* MODE */
    {
      SettingsConfigEntry *ikbd_mode_param =
          settings_find_entry(gconfig_getContext(), PARAM_MODE);
      int ikbd_mode = atoi(ikbd_mode_param->value);
      printed = snprintf(pcInsert, iInsertLen, "%i", ikbd_mode);
      break;
    }
    case 8: /* JUSB */
    {
      SettingsConfigEntry *entry =
          settings_find_entry(gconfig_getContext(), PARAM_JOYSTICK_USB);
      const char *val = (entry && entry->value) ? entry->value : "false";
      printed = snprintf(pcInsert, iInsertLen, "%s", val);
      break;
    }
    case 9: /* JPORT */
    {
      SettingsConfigEntry *entry =
          settings_find_entry(gconfig_getContext(), PARAM_JOYSTICK_USB_PORT);
      const char *val = (entry && entry->value) ? entry->value : "1";
      printed = snprintf(pcInsert, iInsertLen, "%s", val);
      break;
    }
    case 10: /* MORIG */
    {
      SettingsConfigEntry *entry =
          settings_find_entry(gconfig_getContext(), PARAM_MOUSE_ORIGINAL);
      const char *val = (entry && entry->value) ? entry->value : "false";
      printed = snprintf(pcInsert, iInsertLen, "%s", val);
      break;
    }
    case 11: /* MSPEED */
    {
      SettingsConfigEntry *entry =
          settings_find_entry(gconfig_getContext(), PARAM_MOUSE_SPEED);
      const char *val = (entry && entry->value) ? entry->value : "5";
      printed = snprintf(pcInsert, iInsertLen, "%s", val);
      break;
    }
    case 12: /* KBLANG */
    {
      SettingsConfigEntry *entry =
          settings_find_entry(gconfig_getContext(), PARAM_USB_KB_LAYOUT);
      const char *val = (entry && entry->value) ? entry->value : "en";
      char lower_buf[8];
      to_lowercase_copy(val, lower_buf, sizeof(lower_buf));
      printed = snprintf(pcInsert, iInsertLen, "%s", lower_buf);
      break;
    }
    case 13: /* BTKBL */
    {
      SettingsConfigEntry *entry =
          settings_find_entry(gconfig_getContext(), PARAM_BT_KB_LAYOUT);
      const char *val = (entry && entry->value) ? entry->value : "en";
      char lower_buf[8];
      to_lowercase_copy(val, lower_buf, sizeof(lower_buf));
      printed = snprintf(pcInsert, iInsertLen, "%s", lower_buf);
      break;
    }
    case 14: /* CTARGET */
      printed = snprintf(pcInsert, iInsertLen, "%d", COMPUTER_TARGET);
      break;
    case 15: /* BTGSHT */
    {
      SettingsConfigEntry *entry =
          settings_find_entry(gconfig_getContext(), PARAM_BT_GAMEPADSHOOT);
      const char *val = (entry && entry->value) ? entry->value : "0";
      printed = snprintf(pcInsert, iInsertLen, "%s", val);
      break;
    }
    case 16: /* JASHT */
    {
      SettingsConfigEntry *entry =
          settings_find_entry(gconfig_getContext(), PARAM_JOYSTICK_USB_AUTOSHOOT);
      const char *val = (entry && entry->value) ? entry->value : "0";
      printed = snprintf(pcInsert, iInsertLen, "%s", val);
      break;
    }
    case 17: /* WFIMODE */
    {
      SettingsConfigEntry *entry =
          settings_find_entry(gconfig_getContext(), PARAM_WIFI_MODE);
      const char *val = (entry && entry->value) ? entry->value : "0";
      printed = snprintf(pcInsert, iInsertLen, "%s", val);
      break;
    }
    case 18: /* WFIHOST */
    {
      SettingsConfigEntry *entry =
          settings_find_entry(gconfig_getContext(), PARAM_HOSTNAME);
      const char *val = (entry && entry->value) ? entry->value : "croissant";
      printed = snprintf(pcInsert, iInsertLen, "%s", val);
      break;
    }
    case 19: /* WFISSID */
    {
      SettingsConfigEntry *entry =
          settings_find_entry(gconfig_getContext(), PARAM_WIFI_SSID);
      const char *val = (entry && entry->value) ? entry->value : "";
      printed = snprintf(pcInsert, iInsertLen, "%s", val);
      break;
    }
    case 20: /* WFIPASS */
    {
      SettingsConfigEntry *entry =
          settings_find_entry(gconfig_getContext(), PARAM_WIFI_PASSWORD);
      const char *val = (entry && entry->value) ? entry->value : "";
      printed = snprintf(pcInsert, iInsertLen, "%s", val);
      break;
    }
    case 21: /* WFIAUTH */
    {
      SettingsConfigEntry *entry =
          settings_find_entry(gconfig_getContext(), PARAM_WIFI_AUTH);
      const char *val = (entry && entry->value) ? entry->value : "0";
      printed = snprintf(pcInsert, iInsertLen, "%s", val);
      break;
    }
    case 22: /* WDFHOST */
    {
      printed = snprintf(pcInsert, iInsertLen, "%s", WIFI_AP_HOSTNAME);
      break;
    }
    case 23: /* WDFPASS */
    {
      printed = snprintf(pcInsert, iInsertLen, "%s", WIFI_AP_PASS);
      break;
    }
    case 24: /* WDFAUTH */
    {
      printed = snprintf(pcInsert, iInsertLen, "%d", WIFI_AP_AUTH);
      break;
    }
    default: /* unknown tag */
      printed = 0;
      break;
  }
  LWIP_ASSERT("sane length", printed <= 0xFFFF);
  return (u16_t)printed;
}

// The main function should be as follows:
void mngr_httpd_start() {
  // Initialize the HTTP server with SSI tags and CGI handlers
  httpd_server_init(ssi_tags, LWIP_ARRAYSIZE(ssi_tags), ssi_handler,
                    cgi_handlers, LWIP_ARRAYSIZE(cgi_handlers));
}
