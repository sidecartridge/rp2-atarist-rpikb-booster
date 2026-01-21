/**
 * File: version.c
 * Author: Diego Parrilla Santamaría
 * Date: January 2025
 * Copyright: 2025 - GOODDATA LABS SL
 * Description: Version helper functions.
 */

#include "version.h"

const char *version_get_string(void) { return RELEASE_VERSION; }

bool version_isNewer(void) { return false; }
