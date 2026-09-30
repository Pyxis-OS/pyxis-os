#ifndef KERNEL_USER_LAUNCH_H
#define KERNEL_USER_LAUNCH_H

#include <kernel/mm/types.h>

struct process;
struct mount_config;
struct space;

#define USER_INITIAL_STACK_BASE UINT64_C(0x800000)
/* Fixed, eagerly backed userspace stack; the page below stays reserved/unmapped.
 * No automatic growth. Boot and ordinary launches use the same layout. */
#define USER_INITIAL_STACK_SIZE (1024 * 1024)
#define USER_INITIAL_STACK_GUARD_BASE (USER_INITIAL_STACK_BASE - PAGE_SIZE)

/* BSP, IF=0. Stable kernel image bytes; creates an owned, inactive process and
 * its initial stack. No startup/grants or task submission. Clears outputs and
 * unwinds on failure. Shared by boot and capability-authorized launch. */
enum mm_result user_process_load(struct space *space, const void *bytes, size_t size,
                                  struct process **process, uintptr_t *entry);

/* BSP, IF=0, before scheduler startup. Select one trusted init per workload
 * CPU from the copied boot command line. CPU 0 is used only on a single CPU. */
void user_launch_initial(const char *command_line);

/* Boot startup only: load one archive image/script with full bootstrap grants.
 * App and RAM home roots are shared between initial processes. Fatal on failure. */
void user_launch_init(size_t cpu_index, const char *image_uri,
    const struct mount_config *mount_config);

#endif
