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

/* BSP, IF=0, before scheduler startup. Create each configured space in command
 * line order and start its trusted init. Fatal on invalid configuration. */
void user_launch_initial(const char *command_line);

/* Initial inits and their interpreters name boot archive entries by this root. */
#define USER_BOOT_ROOT_PREFIX "boot://"
#define USER_BOOT_ROOT_PREFIX_LENGTH (sizeof(USER_BOOT_ROOT_PREFIX) - 1)

/* Boot startup only: load one archive image/script with full bootstrap grants.
 * Boot and RAM tmp roots are shared between initial processes. Fatal on failure. */
void user_launch_init(struct space *space, const char *image_uri,
    const struct mount_config *mount_config, bool install);

#endif
