#ifndef ABI_UEFI_H
#define ABI_UEFI_H

#include <abi/message.h>

#define UEFI_RIGHT_BOOT_REGISTER (UINT64_C(1) << 0)
#define UEFI_RIGHTS UEFI_RIGHT_BOOT_REGISTER

#define UEFI_BOOT_REGISTER UINT64_C(1)
#define UEFI_BOOT_FIRST UINT32_C(1)

/* PROTOCOL_UEFI. Register the fixed Pyxis loader on one reviewed GPT ESP.
 * The holder supplies the partition facts; the kernel writes only a Pyxis OS
 * Boot#### option for \EFI\pyxis\BOOTX64.EFI and appends it to BootOrder, or
 * moves it first when requested. An exact existing option is reused. Foreign
 * entries retain their relative order. Success requires readback of every
 * changed variable. OUTCOME_UNKNOWN means no automatic retry is safe. */
struct uefi_boot_register_request {
  uint64_t first_lba, block_count;
  uint8_t partition_guid[16];
  uint32_t partition_number;
  uint32_t flags;
};

_Static_assert(sizeof(struct uefi_boot_register_request) == 40,
    "UEFI boot request layout");

#endif
