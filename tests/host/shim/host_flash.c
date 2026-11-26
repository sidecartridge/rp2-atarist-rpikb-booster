/* The flash behind shim/hardware/flash.h. Erasing sets bytes to 0xFF and
 * programming can only clear bits, as on the real chip. */
#include <stdio.h>
#include <stdlib.h>

#include "hardware/flash.h"

uint8_t host_flash[HOST_FLASH_SIZE];

static void check_range(const char *what, uint32_t offs, size_t count,
                        size_t align) {
  if (offs % align != 0 || count % align != 0 ||
      (size_t)offs + count > HOST_FLASH_SIZE) {
    fprintf(stderr, "%s: bad range 0x%x + %zu\n", what, (unsigned)offs, count);
    abort();
  }
}

void flash_range_erase(uint32_t flash_offs, size_t count) {
  check_range("flash_range_erase", flash_offs, count, FLASH_SECTOR_SIZE);
  for (size_t i = 0; i < count; i++) host_flash[flash_offs + i] = 0xFF;
}

void flash_range_program(uint32_t flash_offs, const uint8_t *data,
                         size_t count) {
  check_range("flash_range_program", flash_offs, count, FLASH_PAGE_SIZE);
  for (size_t i = 0; i < count; i++) host_flash[flash_offs + i] &= data[i];
}
