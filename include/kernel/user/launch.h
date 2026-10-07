#ifndef KERNEL_USER_LAUNCH_H
#define KERNEL_USER_LAUNCH_H

#include <kernel/mm/types.h>

struct boot_options;
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

/* BSP, IF=0, before scheduler startup. Enables requested UDP logging and
 * starts boot init using the previously parsed boot options. */
void user_launch_initial(const struct boot_options *options);

/* Initial inits and their interpreters name boot archive entries by this root. */
#define USER_BOOT_ROOT_PREFIX "boot://"
#define USER_BOOT_ROOT_PREFIX_LENGTH (sizeof(USER_BOOT_ROOT_PREFIX) - 1)

/* Boot startup only: load boot init, an archive image or script, in Caelum's
 * space with the bootstrap services, the boot and RAM tmp roots, the space
 * factory, mount authority and, with INSTALL, the raw installer grants. It
 * gets console output but no input or other space devices. Its arguments add
 * --installed when a disk is bound and --default-config for the rescue entry.
 * Reverse mode adds --remote-beacon NAME; boot init selects the Remote space.
 * Fatal on failure. */
void user_launch_boot_init(const char *image_uri, const struct mount_config *mount_config,
    bool install, bool default_config, const char *remote_beacon);

#endif
