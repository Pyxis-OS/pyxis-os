#ifndef KERNEL_USER_LAUNCH_H
#define KERNEL_USER_LAUNCH_H

#include <kernel/mm/types.h>

struct boot_options;
struct process;
struct mount_config;
struct space;

/* Boot CPU0 IF=0, before admitting runtime callers. Allocates mandatory queues. */
void launcher_ram_reapers_init(void);
/* Current CPU IF=0 query; its fixed IF=1 RAM actor owns deferred whole batches. */
bool launcher_ram_retire_pending(void);
void launcher_reap_prepared_ram(void);

/* Pure admission: zero selects the default; explicit sizes obey launcher ABI
 * bounds and page alignment. Clears SIZE on failure. No allocation or VM work. */
enum mm_result user_initial_stack_size(uint64_t requested, size_t *size);

/* BSP, IF=0. Stable kernel image bytes; creates an owned, inactive process and
 * its eagerly backed initial stack with a reserved unmapped lower guard.
 * INITIAL_STACK_BYTES uses the same admission as user_initial_stack_size.
 * Returns the actual stack top; no growth, startup/grants or task submission.
 * Clears outputs and unwinds on failure. Shared by boot and ordinary launch. */
enum mm_result user_process_load(struct space *space, const void *bytes, size_t size,
    uint64_t initial_stack_bytes, struct process **process, uintptr_t *entry,
    uintptr_t *stack_top);

/* BSP, IF=0, before scheduler startup. Enables requested UDP logging and
 * starts boot init using the previously parsed boot options. */
void user_launch_initial(const struct boot_options *options);

/* Initial inits and their interpreters name boot archive entries by this root. */
#define USER_BOOT_ROOT_PREFIX "boot://"
#define USER_BOOT_ROOT_PREFIX_LENGTH (sizeof(USER_BOOT_ROOT_PREFIX) - 1)
#define USER_DEFAULT_BOOT_INIT USER_BOOT_ROOT_PREFIX "boot-init.pxe"

/* Boot startup only: load boot init, an archive image or script, in Caelum's
 * space with the bootstrap services, the boot and RAM tmp roots, the space
 * factory, mount authority and, with INSTALL, the raw installer grants. It
 * gets console output but no input or other space devices. Its arguments add
 * --installed when a disk is bound, --pool N for that disk's mount.pool
 * partition or --pool-malformed when its value is malformed, and
 * --default-config for the rescue entry.
 * Reverse mode adds --remote-beacon NAME; boot init selects the Remote space.
 * Fatal on failure. */
void user_launch_boot_init(const char *image_uri, const struct mount_config *mount_config,
    uint32_t pool_partition, bool pool_malformed, bool install, bool default_config,
    const char *remote_beacon);

#endif
