#ifndef KERNEL_OBJECT_MOUNT_H
#define KERNEL_OBJECT_MOUNT_H

#include <abi/syscall.h>
#include <kernel/gpt.h>
#include <kernel/boot.h>
#include <kernel/object/object.h>
#include <stdatomic.h>

/* BSP/IF=0: one owned reference to authority over the selected boot-lifetime
 * virtio-fs export. Creation does not wait for transport initialization. */
struct kernel_object *mount_create(void);
struct kernel_object *mount_create_ram_npfs(void);

struct mount_config {
  bool enabled;
  struct gpt_guid disk;
};

struct fat32_node;
enum mount_backend { MOUNT_HOST, MOUNT_NATIVE, MOUNT_BOOT_ESP, MOUNT_FAT32, MOUNT_RAM_NPFS };
struct mount_object {
  struct kernel_object object;
  enum mount_backend backend;
  struct mount_config config;
  struct boot_origin origin;
  /* Boot ESP issuer only: one successful upgrade admission per boot, shared by
   * every copy of the issuer. */
  atomic_bool upgrade_admitted;
  struct fat32_node *fat32;
};
void mount_set_boot_origin(const struct boot_origin *origin);
/* The issuer also keeps the native pool's configured disk for ADMIT. */
struct kernel_object *mount_create_boot_esp(const struct mount_config *pool);
void mount_init_fat32(struct mount_object *mount, struct fat32_node *node);
/* UPGRADE_BOOT_ESP exists only on the boot-origin ESP issuer. */
bool mount_rights_valid(const struct mount_object *mount, uint64_t rights);

/* Trusted boot configuration is captured; authority owns no backing roots. */
struct kernel_object *mount_create_native(const struct mount_config *config);
struct syscall_result mount_call(struct kernel_object *object, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address,
    size_t reply_capacity);

/* Root through a trusted physical-device capability, outside any raw claim on
 * its partition. WRITABLE admits mutation rights, as mount WRITE does. */
struct disk_object;
struct syscall_result mount_open_device(struct disk_object *disk, bool writable,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity);

#endif
