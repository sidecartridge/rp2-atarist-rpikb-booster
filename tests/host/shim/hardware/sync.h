/* Host stand-in for the Pico SDK's hardware/sync.h: there are no interrupts
 * to disable on the host. */
#ifndef HOST_SHIM_HARDWARE_SYNC_H
#define HOST_SHIM_HARDWARE_SYNC_H

#include <stdint.h>

static inline uint32_t save_and_disable_interrupts(void) { return 0; }
static inline void restore_interrupts(uint32_t status) { (void)status; }

#endif
