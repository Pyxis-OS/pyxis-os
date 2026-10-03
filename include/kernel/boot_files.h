#ifndef KERNEL_BOOT_FILES_H
#define KERNEL_BOOT_FILES_H

#include <kernel/initrd.h>
#include <kernel/mm/types.h>

struct boot_info;

/* Once on the BSP, IF=0, after initrd_init and before AP startup. Retain the
 * original ELF through a supervisor read-only/NX mapping of boot-reserved
 * frames, and reuse the whole archive mapping. No file bytes are copied.
 * Failure removes partial ELF mappings and its virtual reservation. */
enum mm_result boot_files_init(const struct boot_info *boot);

/* NULL before successful initialization; otherwise immutable borrowed views
 * valid for the kernel lifetime. Neither storage nor views may be freed. */
const struct initrd_file *boot_files_kernel(void);
const struct initrd_file *boot_files_archive(void);

#endif
