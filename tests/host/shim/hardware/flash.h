/* Host stand-in for the Pico SDK's hardware/flash.h: host_flash, a RAM array
 * the size of the RP2350's 4 MB flash, plays the flash and is mapped at
 * XIP_BASE. Like the real calls, erase and program take offsets from the
 * start of flash, and abort on a range the real flash would reject. */
#ifndef HOST_SHIM_HARDWARE_FLASH_H
#define HOST_SHIM_HARDWARE_FLASH_H

#include <stddef.h>
#include <stdint.h>

#define FLASH_PAGE_SIZE (1u << 8)
#define FLASH_SECTOR_SIZE (1u << 12)
#define HOST_FLASH_SIZE (4u * 1024u * 1024u)

extern uint8_t host_flash[HOST_FLASH_SIZE];
#define XIP_BASE ((uintptr_t)host_flash)

void flash_range_erase(uint32_t flash_offs, size_t count);
void flash_range_program(uint32_t flash_offs, const uint8_t *data,
                         size_t count);

#endif
