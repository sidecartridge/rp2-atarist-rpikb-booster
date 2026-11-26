/* The mbedtls configuration for the host tests: only the base64 module, the
 * one part of mbedtls Booster's web request decoding uses. */
#ifndef HOST_SHIM_MBEDTLS_CONFIG_H
#define HOST_SHIM_MBEDTLS_CONFIG_H
#define MBEDTLS_BASE64_C
#endif
