#include <arch/cpu.h>
#include <arch/cpu_local.h>
#include <arch/pci.h>
#include <kernel/boot/options.h>
#include <kernel/initrd.h>
#include <kernel/panic.h>
#include <kernel/pci/registers.h>
#include "rtl8922ae.h"
#include "rtl8922ae_firmware.h"

#define RTL8922AE_VENDOR 0x10ecu
#define RTL8922AE_DEVICE 0x8922u
#define RTL8922AE_BAR 2u
#define RTL8922AE_FIRMWARE_BYTES 1849226u

static struct {
  struct wifi_status_reply status;
  struct rtl8922ae_firmware_info firmware;
  uint16_t command, pm_control, msi_control;
  unsigned pm, msi, pcie;
  uint64_t bar_address;
  uint32_t bar_encoding;
} preparation;

static void set_detail(const char *detail)
{
  size_t i = 0;
  while (i + 1 < sizeof(preparation.status.detail) && detail[i]) {
    preparation.status.detail[i] = detail[i];
    ++i;
  }
  preparation.status.detail[i] = '\0';
}

static bool inspect_capabilities(struct pci_address address)
{
  if (!(pci_read16(address, PCI_STATUS) & PCI_STATUS_CAPABILITIES)) {
    return false;
  }
  bool seen[PCI_CONVENTIONAL_BYTES / PCI_REGISTER_BYTES] = {0};
  unsigned offset = pci_read8(address, PCI_CAPABILITIES) & PCI_CAP_POINTER_MASK;
  while (offset) {
    if (offset < PCI_CAP_FIRST || offset > PCI_CONVENTIONAL_BYTES - PCI_REGISTER_BYTES ||
        seen[offset / PCI_REGISTER_BYTES]) {
      return false;
    }
    seen[offset / PCI_REGISTER_BYTES] = true;
    uint8_t id = pci_read8(address, offset + PCI_CAP_ID);
    if (id == PCI_CAP_MSI) {
      if (preparation.msi) {
        return false;
      }
      preparation.msi = offset;
      preparation.msi_control = pci_read16(address, offset + PCI_MSI_CONTROL);
    } else if (id == PCI_CAP_POWER) {
      if (preparation.pm || offset > PCI_CONVENTIONAL_BYTES - PCI_POWER_BYTES) {
        return false;
      }
      preparation.pm = offset;
      preparation.pm_control = pci_read16(address, offset + PCI_POWER_CONTROL);
    } else if (id == PCI_CAP_EXPRESS) {
      if (preparation.pcie) {
        return false;
      }
      preparation.pcie = offset;
    }
    offset = pci_read8(address, offset + PCI_CAP_NEXT) & PCI_CAP_POINTER_MASK;
  }
  return preparation.pm && preparation.msi && preparation.pcie &&
      preparation.pm_control != UINT16_MAX && preparation.msi_control != UINT16_MAX;
}

void rtl8922ae_prepare(void)
{
  KASSERT(cpu_current() == cpu_bsp() && !cpu_interrupts_enabled());
  preparation.status.flags = WIFI_ENABLED;
  if (boot_options_get()->wifi_ax200) {
    set_detail("Select only one Wi-Fi backend; both radios left untouched");
    return;
  }
  struct pci_device *device = NULL;
  enum pci_selection selection = pci_select_device(RTL8922AE_VENDOR, RTL8922AE_DEVICE, &device);
  if (selection != PCI_SELECTION_UNIQUE) {
    set_detail(selection == PCI_SELECTION_ABSENT ? "RTL8922AE absent" :
        "RTL8922AE inventory unavailable or ambiguous");
    return;
  }
  preparation.status.flags |= WIFI_PRESENT;
  for (size_t i = 0; i < pci_device_count(); ++i) {
    if (pci_device_at(i) == device) {
      preparation.status.controller_id = (uint32_t)i + 1;
      break;
    }
  }
  preparation.status.pci_bus = device->address.bus;
  preparation.status.pci_device = device->address.device;
  preparation.status.pci_function = device->address.function;
  preparation.command = pci_read16(device->address, PCI_COMMAND);
  unsigned bar_offset = PCI_BAR_FIRST + RTL8922AE_BAR * PCI_REGISTER_BYTES;
  preparation.bar_encoding = pci_read32(device->address, bar_offset);
  uint32_t type = preparation.bar_encoding & PCI_BAR_MEMORY_TYPE_MASK;
  if (preparation.command == UINT16_MAX ||
      preparation.bar_encoding == UINT32_MAX || (preparation.bar_encoding & PCI_BAR_IO) ||
      (type != PCI_BAR_MEMORY_32 && type != PCI_BAR_MEMORY_64) ||
      !inspect_capabilities(device->address)) {
    preparation.status.flags |= WIFI_FAILED;
    set_detail("RTL8922AE PCI metadata unsupported; device untouched");
    return;
  }
  preparation.bar_address = preparation.bar_encoding & PCI_BAR_MEMORY_ADDRESS_MASK;
  if (type == PCI_BAR_MEMORY_64) {
    preparation.bar_address |= (uint64_t)pci_read32(device->address,
        bar_offset + PCI_REGISTER_BYTES) << PCI_BAR_HIGH_SHIFT;
  }
  /* BAR encoding is an observation, not a sized aperture or a device claim.
   * The archive view stays immutable for the boot; no bytes reach hardware. */
  struct initrd_file file;
  if (initrd_lookup("firmware/rtl8922ae/rtw8922a_fw-4.bin", &file) != INITRD_OK ||
      file.size != RTL8922AE_FIRMWARE_BYTES ||
      !rtl8922ae_firmware_inspect(file.data, file.size, &preparation.firmware)) {
    preparation.status.flags |= WIFI_FAILED;
    set_detail("RTL8922AE pinned firmware unavailable or malformed; device untouched");
    return;
  }
  preparation.status.flags |= WIFI_PREPARED;
  set_detail("RTL8922AE CPU-only preparation; firmware/radio unavailable; warm state unknown");
}

void rtl8922ae_status(struct wifi_status_reply *status)
{
  *status = preparation.status;
}
