#include <arch/layout.h>
#include <arch/pci.h>
#include <kernel/panic.h>

#define ECAM_BUS_SHIFT 20
#define ECAM_DEVICE_SHIFT 15
#define ECAM_FUNCTION_SHIFT 12

static unsigned mapped_buses;

void arch_pci_init(unsigned bus_count)
{
  KASSERT(bus_count <= PCI_BUS_COUNT);
  mapped_buses = bus_count;
}

unsigned arch_pci_bus_count(void)
{
  return mapped_buses;
}

static uintptr_t config_address(struct pci_address address, unsigned offset,
                                 unsigned bytes)
{
  KASSERT(address.bus < mapped_buses && address.device < PCI_DEVICE_COUNT &&
          address.function < PCI_FUNCTION_COUNT);
  KASSERT(offset <= PCI_CONFIG_BYTES - bytes && !(offset % bytes));

  /* Each function occupies 4 KiB; the next bits select function, device, bus.
   * The fixed mapping begins at bus zero, just like the supported MCFG base. */
  return PCI_ECAM_BASE + ((uintptr_t)address.bus << ECAM_BUS_SHIFT) +
    ((uintptr_t)address.device << ECAM_DEVICE_SHIFT) +
    ((uintptr_t)address.function << ECAM_FUNCTION_SHIFT) + offset;
}

uint8_t pci_read8(struct pci_address address, unsigned offset)
{
  return *(const volatile uint8_t *)config_address(address, offset, sizeof(uint8_t));
}

uint16_t pci_read16(struct pci_address address, unsigned offset)
{
  return *(const volatile uint16_t *)config_address(address, offset, sizeof(uint16_t));
}

uint32_t pci_read32(struct pci_address address, unsigned offset)
{
  return *(const volatile uint32_t *)config_address(address, offset, sizeof(uint32_t));
}
