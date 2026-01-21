/**
 * File: mngr_httpd.h
 * Author: Diego Parrilla Santamaría
 * Date: December 2025
 * Copyright: 2024-2025 - GOODDATA LABS SL
 * Description: Header file for the manager mode httpd server.
 */

#ifndef MNGR_HTTPD_H
#define MNGR_HTTPD_H

typedef enum {
  MNGR_HTTPD_RESPONSE_OK = 200,
  MNGR_HTTPD_RESPONSE_BAD_REQUEST = 400,
  MNGR_HTTPD_RESPONSE_NOT_FOUND = 404,
  MNGR_HTTPD_RESPONSE_INTERNAL_SERVER_ERROR = 500
} mngr_httpd_response_status_t;

void mngr_httpd_start();

#endif  // MNGR_HTTPD_H
