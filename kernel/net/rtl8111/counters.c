#include <arch/clock.h>
#include <arch/cpu.h>
#include <arch/cpu_local.h>
#include <arch/dma.h>
#include <kernel/panic.h>
#include "internal.h"

#define RTL_COUNTER_LOW 0x10
#define RTL_COUNTER_HIGH 0x14
#define RTL_COUNTER_RESET (1u << 0)
#define RTL_COUNTER_DUMP (1u << 3)
#define RTL_COUNTER_TIMEOUT_NS UINT64_C(10000000)

const struct rtl_counters *rtl8111_capture_counters(struct rtl8111_controller *controller)
{
  KASSERT(cpu_current() == cpu_bsp() && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  if (!controller) {
    return NULL;
  }
  if (!controller->active || !controller->counters.address) {
    return NULL;
  }
  uint8_t command = rtl_read8(controller, RTL_CHIP_COMMAND);
  if (command == UINT8_MAX || !(command & RTL_COMMAND_RX) ||
      (rtl_read32(controller, RTL_COUNTER_LOW) & (RTL_COUNTER_RESET | RTL_COUNTER_DUMP))) {
    return NULL;
  }

  phys_addr_t physical = controller->counters.physical;
  rtl_write32(controller, RTL_COUNTER_HIGH, (uint32_t)(physical >> 32));
  (void)rtl_read8(controller, RTL_CHIP_COMMAND);
  rtl_write32(controller, RTL_COUNTER_LOW, (uint32_t)physical);
  rtl_write32(controller, RTL_COUNTER_LOW, (uint32_t)physical | RTL_COUNTER_DUMP);
  uint64_t start = arch_monotonic_ns();
  do {
    if (!(rtl_read32(controller, RTL_COUNTER_LOW) & (RTL_COUNTER_RESET | RTL_COUNTER_DUMP))) {
      dma_read_barrier();
      return (const struct rtl_counters *)controller->counters.address;
    }
    __asm__ volatile("pause");
  } while (arch_monotonic_ns() - start < RTL_COUNTER_TIMEOUT_NS);
  return NULL;
}
