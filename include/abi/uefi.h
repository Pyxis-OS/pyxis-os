#ifndef ABI_UEFI_H
#define ABI_UEFI_H

#include <abi/message.h>

#define UEFI_RIGHT_BOOT_REGISTER (UINT64_C(1) << 0)
#define UEFI_RIGHT_BOOT_MANAGE (UINT64_C(1) << 1)
#define UEFI_RIGHTS (UEFI_RIGHT_BOOT_REGISTER | UEFI_RIGHT_BOOT_MANAGE)

#define UEFI_BOOT_REGISTER UINT64_C(1)
#define UEFI_BOOT_FIRST UINT32_C(1)

/* Boot manager operations, all under UEFI_RIGHT_BOOT_MANAGE. */
#define UEFI_BOOT_VARIABLE_GET UINT64_C(2)
#define UEFI_BOOT_VARIABLE_SET UINT64_C(3)
#define UEFI_BOOT_VARIABLE_DELETE UINT64_C(4)
#define UEFI_BOOT_ENTRIES UINT64_C(5)

enum uefi_boot_variable_kind {
  UEFI_VARIABLE_BOOT_ENTRY = 1, /* Boot#### for number 0..0xffff */
  UEFI_VARIABLE_BOOT_ORDER = 2,
  UEFI_VARIABLE_BOOT_NEXT = 3,
  UEFI_VARIABLE_BOOT_CURRENT = 4, /* read-only */
  UEFI_VARIABLE_TIMEOUT = 5,
};

#define UEFI_BOOT_VARIABLE_BYTES 4096u
#define UEFI_BOOT_ENTRY_WINDOW 256u
#define UEFI_BOOT_ENTRY_NUMBERS 65536u

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

/* PROTOCOL_UEFI boot manager. The holder reads and writes only the firmware's
 * global-GUID boot variables named by kind (number selects Boot####). Values
 * are raw EFI bytes; the holder builds and parses load options and BootOrder.
 * Written variables use the standard NV | boot services | runtime attributes.
 *
 * GET returns struct uefi_boot_variable_reply followed by size bytes; NOT_FOUND
 * when absent, LIMIT when reply_capacity is too small for the value. SET takes
 * struct uefi_boot_variable_set_request followed by size bytes (1..
 * UEFI_BOOT_VARIABLE_BYTES). DELETE takes struct uefi_boot_variable_name and
 * returns NOT_FOUND for an absent variable. BOOT_CURRENT is read-only.
 * Success means readback of the written (or deleted) variable matched. A
 * failed or unverified write returns OUTCOME_UNKNOWN: inspect the variable
 * before another attempt. ENTRIES reports which Boot#### numbers in
 * [first, first + count) exist as a bitmap, bit n of byte n / 8, count at most
 * UEFI_BOOT_ENTRY_WINDOW, so no single call holds firmware for long. */
struct uefi_boot_variable_name {
  uint32_t kind;
  uint32_t number;
};

struct uefi_boot_variable_set_request {
  struct uefi_boot_variable_name name;
  uint32_t size;
  uint32_t reserved;
};

struct uefi_boot_variable_reply {
  uint32_t attributes;
  uint32_t size;
};

struct uefi_boot_entries_request {
  uint32_t first;
  uint32_t count;
};

_Static_assert(sizeof(struct uefi_boot_variable_name) == 8 &&
    sizeof(struct uefi_boot_variable_set_request) == 16 &&
    sizeof(struct uefi_boot_variable_reply) == 8 &&
    sizeof(struct uefi_boot_entries_request) == 8, "UEFI boot manager layouts");

#endif
