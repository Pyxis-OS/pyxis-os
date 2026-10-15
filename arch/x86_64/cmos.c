#include <arch/cmos.h>
#include <arch/cpu.h>
#include <arch/smp.h>
#include <kernel/panic.h>

#define CMOS_INDEX_PORT 0x70
#define CMOS_DATA_PORT 0x71
#define CMOS_REGISTER_COUNT 128u

bool arch_cmos_transfer(bool write, uint64_t offset, unsigned bytes, uint64_t *value)
{
  if (!value || (bytes != 1 && bytes != 2 && bytes != 4 && bytes != 8) ||
      offset >= CMOS_REGISTER_COUNT || bytes > CMOS_REGISTER_COUNT - offset) {
    return false;
  }

  uint64_t flags = cpu_save_interrupts();
  KASSERT(arch_cpu_index() == 0);
  uint64_t result = 0;
  for (unsigned i = 0; i < bytes; ++i) {
    /* Bit 7 is deliberately clear; port 0x70 is not a saved-state readback. */
    outb(CMOS_INDEX_PORT, (uint8_t)(offset + i));
    if (write) {
      outb(CMOS_DATA_PORT, (uint8_t)(*value >> (i * 8)));
    } else {
      result |= (uint64_t)inb(CMOS_DATA_PORT) << (i * 8);
    }
  }
  if (!write) {
    *value = result;
  }
  cpu_restore_interrupts(flags);
  return true;
}
