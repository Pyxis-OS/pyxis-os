#ifndef ARCH_UEFI_H
#define ARCH_UEFI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define EFI_STATUS_ERROR (UINT64_C(1) << 63)
#define EFI_STATUS_UNSUPPORTED (EFI_STATUS_ERROR | UINT64_C(3))
#define EFI_STATUS_BUFFER_TOO_SMALL (EFI_STATUS_ERROR | UINT64_C(5))
#define EFI_STATUS_NOT_FOUND (EFI_STATUS_ERROR | UINT64_C(14))

struct efi_guid {
  uint32_t part1;
  uint16_t part2, part3;
  uint8_t part4[8];
};

enum arch_uefi_gate_result {
  ARCH_UEFI_READY,
  ARCH_UEFI_UNAVAILABLE,
  ARCH_UEFI_BUSY,
};

/* Paging initializes these from the validated Limine handoff, before AP start. */
void arch_uefi_enable(uintptr_t get_variable, uintptr_t set_variable);
bool arch_uefi_available(void);
/* A nonblocking, any-CPU gate. No scheduler or user copy runs while held. */
enum arch_uefi_gate_result arch_uefi_begin(uint64_t *interrupt_flags);
void arch_uefi_end(uint64_t interrupt_flags);
uint64_t arch_uefi_get_variable(const uint16_t *name, const struct efi_guid *guid,
    uint32_t *attributes, size_t *size, void *bytes);
uint64_t arch_uefi_set_variable(const uint16_t *name, const struct efi_guid *guid,
    uint32_t attributes, size_t size, const void *bytes);

#endif
