#ifndef KERNEL_DEBUG_H
#define KERNEL_DEBUG_H

#include <kernel/boot.h>
#include <stdint.h>

struct boot_options;

/* Boot BSP, IF=0, after driver/display preparation and before AP startup. */
void debug_prepare(const struct boot_info *boot,
                    const struct boot_options *options);
/* After random/task initialization, IF=0; opt-in entropy preparation only. */
void debug_start(void);

/* Enabled network worker only. Withdraw readiness around protocol/configuration
 * work; publish a complete frozen candidate at its coherent iteration tail. */
void debug_network_update(bool coherent);
void debug_network_checkpoint(void);
/* GS-independent, published readiness only; terminal entry may still be refused
 * by the native driver if ownership changed before the BSP takes it. */
bool debug_network_ready(void);

/* BSP private entry, IF=0. Begin does no allocation or ordinary service calls.
 * False with no retained ownership permits existing fatal fallback. */
bool debug_stop_begin(uint64_t generation, bool terminal);
bool debug_stop_retained(void);
/* COMPLETE on the BSP debugger IST. Returns only after confirmed restoration
 * for ordinary continue/idle expiry. Terminal/uncertain stops never return. */
void debug_stop_run(uint64_t generation);

#endif
