/**
 * File: version.h
 * Author: Diego Parrilla Santamaría
 * Date: January 2025
 * Copyright: 2025 - GOODDATA LABS SL
 * Description: Helpers to expose compiled firmware version information.
 */

#ifndef VERSION_H
#define VERSION_H

#include <stdbool.h>

#include "constants.h"

/**
 * @brief Get the compiled firmware version string.
 *
 * @return const char* pointing to the RELEASE_VERSION macro.
 */
const char *version_get_string(void);

/**
 * @brief Stub that indicates whether a newer version is available.
 *
 * @return false until version-check logic is implemented.
 */
bool version_isNewer(void);

#endif  // VERSION_H
