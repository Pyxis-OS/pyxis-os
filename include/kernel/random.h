#ifndef KERNEL_RANDOM_H
#define KERNEL_RANDOM_H

#include <abi/random.h>
#include <abi/syscall.h>
#include <kernel/boot.h>

/* BSP/IF=0: select and prepare before AP startup, start after task_init().
 * VirtIO presence takes priority; failed preparation never selects CPU entropy. */
void random_prepare(const struct boot_info *boot);
void random_start(void);
/* BSP interrupt entry, IF=0: notify the source-neutral worker. */
void random_notify(void);
/* Current task, IF=0. Copies out only on complete success. Private caller
 * storage stays local; the BSP worker owns bounded shared staging slots. */
enum call_status random_read(void *bytes, size_t length, uint64_t deadline);

#endif
