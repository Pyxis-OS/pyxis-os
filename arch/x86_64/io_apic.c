#include <arch/acpi.h>
#include <arch/apic.h>
#include <arch/io_apic.h>
#include <arch/layout.h>
#include <kernel/log.h>
#include <kernel/panic.h>

#define IO_APIC_SELECT 0x00
#define IO_APIC_WINDOW 0x10
#define IO_APIC_VERSION 0x01
#define IO_APIC_MAX_ENTRY_SHIFT 16
#define IO_APIC_MAX_ENTRY_MASK 0xff
#define IO_APIC_REDIRECTION_BASE 0x10
#define IO_APIC_REDIRECTION_REGISTERS 2
#define IO_APIC_DESTINATION_SHIFT 24
#define IO_APIC_ACTIVE_LOW (1u << 13)
#define IO_APIC_LEVEL_TRIGGERED (1u << 15)
#define IO_APIC_MASKED (1u << 16)

static struct keyboard_irq_route keyboard_route;
static uint32_t keyboard_register;
static uint32_t keyboard_entry;
static bool initialized;

static uint32_t io_apic_read(uint32_t reg)
{
  *(volatile uint32_t *)(IO_APIC_BASE + IO_APIC_SELECT) = reg;
  return *(volatile uint32_t *)(IO_APIC_BASE + IO_APIC_WINDOW);
}

static void io_apic_write(uint32_t reg, uint32_t value)
{
  *(volatile uint32_t *)(IO_APIC_BASE + IO_APIC_SELECT) = reg;
  *(volatile uint32_t *)(IO_APIC_BASE + IO_APIC_WINDOW) = value;
}

void io_apic_prepare(const struct boot_info *boot)
{
  if (!acpi_keyboard_route(boot, &keyboard_route)) {
    keyboard_route = (struct keyboard_irq_route){0};
  }
}

uint64_t io_apic_physical_address(void)
{
  return keyboard_route.io_apic_physical;
}

bool io_apic_init(void)
{
  if (!keyboard_route.io_apic_physical) {
    klog("keyboard: no ACPI route or 8042 controller; input unavailable\n");
    return false;
  }

  unsigned max_entry =
    (io_apic_read(IO_APIC_VERSION) >> IO_APIC_MAX_ENTRY_SHIFT) & IO_APIC_MAX_ENTRY_MASK;
  unsigned pin = keyboard_route.gsi - keyboard_route.gsi_base;
  if (pin > max_entry ||
      max_entry > (UINT8_MAX - IO_APIC_REDIRECTION_BASE) / IO_APIC_REDIRECTION_REGISTERS) {
    panic("keyboard GSI outside supported I/O APIC inputs");
  }

  for (unsigned i = 0; i <= max_entry; ++i) {
    io_apic_write(IO_APIC_REDIRECTION_BASE + i * IO_APIC_REDIRECTION_REGISTERS,
                  IO_APIC_MASKED);
  }

  keyboard_register = IO_APIC_REDIRECTION_BASE + pin * IO_APIC_REDIRECTION_REGISTERS;
  keyboard_entry = APIC_KEYBOARD_VECTOR;
  if (keyboard_route.active_low) {
    keyboard_entry |= IO_APIC_ACTIVE_LOW;
  }
  if (keyboard_route.level_triggered) {
    keyboard_entry |= IO_APIC_LEVEL_TRIGGERED;
  }

  /* Fixed delivery, physical destination: all keyboard interrupts go to the
   * BSP. Keep the route masked until the controller and receive queue are ready. */
  io_apic_write(keyboard_register + 1, apic_id() << IO_APIC_DESTINATION_SHIFT);
  io_apic_write(keyboard_register, keyboard_entry | IO_APIC_MASKED);
  initialized = true;
  klog("keyboard: IRQ 1 -> GSI %u, I/O APIC input %u -> BSP APIC %u\n",
       keyboard_route.gsi, pin, apic_id());
  return true;
}

void io_apic_keyboard_enable(void)
{
  KASSERT(initialized);
  io_apic_write(keyboard_register, keyboard_entry);
}
