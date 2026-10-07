#include <arch/apic.h>
#include <arch/clock.h>
#include <arch/io_apic.h>
#include <arch/layout.h>
#include <arch/paging.h>
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

void arch_pci_config_writable(struct pci_address address, bool writable)
{
  paging_pci_config_writable(config_address(address, 0, 1), writable);
}

void arch_pci_write8(struct pci_address address, unsigned offset, uint8_t value)
{
  *(volatile uint8_t *)config_address(address, offset, sizeof(value)) = value;
}

void arch_pci_write16(struct pci_address address, unsigned offset, uint16_t value)
{
  *(volatile uint16_t *)config_address(address, offset, sizeof(value)) = value;
}

void arch_pci_write32(struct pci_address address, unsigned offset, uint32_t value)
{
  *(volatile uint32_t *)config_address(address, offset, sizeof(value)) = value;
}

bool arch_pci_mmio_available(phys_addr_t physical, size_t bytes)
{
  if (paging_display_aperture_overlaps(physical, bytes)) {
    return false;
  }
  struct page_translation ecam;
  if (arch_page_query(arch_kernel_space(), PCI_ECAM_BASE, &ecam) != MM_OK) {
    return false;
  }
  phys_addr_t reserved[] = {
    apic_physical_address(), io_apic_physical_address(), arch_clock_physical_address(),
  };
  for (size_t i = 0; i < sizeof(reserved) / sizeof(reserved[0]); ++i) {
    if (reserved[i] && physical < reserved[i] + PAGE_SIZE && reserved[i] < physical + bytes) {
      return false;
    }
  }
  return physical >= ecam.physical + mapped_buses * PCI_ECAM_BUS_BYTES ||
    physical + bytes <= ecam.physical;
}
