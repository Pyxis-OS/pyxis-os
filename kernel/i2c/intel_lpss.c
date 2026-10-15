/* SPDX-License-Identifier: MPL-2.0 */
#include <arch/clock.h>
#include <arch/cpu.h>
#include <arch/pci.h>
#include <arch/smp.h>
#include <kernel/i2c/intel_lpss.h>
#include <kernel/i2c/designware.h>
#include <kernel/input/hid_i2c.h>
#include <kernel/log.h>
#include <kernel/panic.h>
#include <kernel/pci/registers.h>
#include <kernel/task.h>

#define INTEL_VENDOR_ID 0x8086
#define PANTHER_LAKE_I2C0_ID 0xe378
#define LPSS_MAPPING_BYTES 4096
#define LPSS_RESETS 0x204
#define LPSS_RESET_FUNCTION 1u
#define LPSS_RESET_APB 2u
#define LPSS_RESET_DMA 4u
#define LPSS_RESET_MASK (LPSS_RESET_FUNCTION | LPSS_RESET_APB | LPSS_RESET_DMA)
#define LPSS_FUNCTION_RUNNING (LPSS_RESET_FUNCTION | LPSS_RESET_APB)
#define LPSS_CAPABILITIES 0x2fc
#define LPSS_CAPABILITIES_TYPE_MASK 0xf0u
#define LPSS_CAPABILITIES_TYPE_I2C 0u
#define LPSS_CLOCK_HZ UINT32_C(100000000)
#define LPSS_SDA_RX_HOLD_MASK UINT32_C(0x00ff0000)
#define LPSS_SDA_RX_HOLD_ONE UINT32_C(0x00010000)
#define LPSS_HANDOFF_TIMEOUT_MS 20

enum lpss_state {
  LPSS_ABSENT,
  LPSS_RESERVED,
  LPSS_PREPARED,
  LPSS_BOUND,
  LPSS_DESCRIPTOR_READY,
  LPSS_REFUSED,
};

/* Boot preparation and the sole BSP worker own this storage until reboot.
 * iDMA stays in reset; there are no DMA allocations or interrupt handlers. */
static struct {
  struct pci_claim claim;
  struct pci_mapping mapping;
  struct pci_probe_state probe;
  struct dw_i2c_master master;
  struct acpi_hid_i2c_endpoint endpoint;
  struct acpi_hid_i2c_pci companion;
  struct hid_i2c_descriptor descriptor;
  enum lpss_state state;
  bool reset_started;
} lpss;

static uint32_t read32(unsigned offset)
{
  return *(volatile uint32_t *)(lpss.mapping.address + offset);
}

static void write32(unsigned offset, uint32_t value)
{
  *(volatile uint32_t *)(lpss.mapping.address + offset) = value;
}

static void preparation_refused(const char *reason)
{
  struct pci_address address = lpss.claim.device->address;
  kinfo("i2c-lpss: %u:%u.%u preparation refused: %s command=0x%x original=0x%x pmcsr=0x%x\n",
      address.bus, address.device, address.function, reason, pci_read16(address, PCI_COMMAND),
      lpss.probe.command, lpss.probe.pmcsr);
  lpss.state = LPSS_REFUSED;
  if (!lpss.reset_started && lpss.claim.reserved) {
    if (lpss.mapping.base) {
      pci_unmap(&lpss.claim, &lpss.mapping);
    }
    if (pci_restore_mmio_probe(&lpss.claim, &lpss.probe)) {
      pci_cancel_reservation(&lpss.claim);
    }
  }
  /* After reset, no restore to an assumed firmware state or speculative
   * runtime release. The still-owned, fenced resources stay until reboot. */
}

void intel_lpss_i2c_prepare(const struct boot_info *boot)
{
  KASSERT(arch_cpu_index() == 0 && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  struct pci_device *device;
  enum pci_selection selection = pci_select_device(INTEL_VENDOR_ID, PANTHER_LAKE_I2C0_ID,
      &device);
  if (selection == PCI_SELECTION_ABSENT) {
    return;
  }
  if (selection != PCI_SELECTION_UNIQUE || device->base_class != 0x0c ||
      device->subclass != 0x80 || (device->header_type & PCI_HEADER_TYPE_MASK) != PCI_HEADER_ENDPOINT) {
    lpss.state = LPSS_REFUSED;
    kinfo("i2c-lpss: compatible controller absent, ambiguous or inventory incomplete\n");
    return;
  }
  if (!pci_reserve_device(device, &lpss.claim)) {
    lpss.state = LPSS_REFUSED;
    kinfo("i2c-lpss: compatible controller reservation refused\n");
    return;
  }
  lpss.state = LPSS_RESERVED;
  if (!pci_begin_mmio_probe(&lpss.claim, &lpss.probe)) {
    preparation_refused("unsafe power/memory-decode state");
    return;
  }
  if (pci_map_bootstrap_bar(&lpss.claim, 0, 0, LPSS_MAPPING_BYTES, boot,
      &lpss.mapping) != MM_OK) {
    preparation_refused("BAR0 bootstrap mapping");
    return;
  }
  uint32_t capabilities = read32(LPSS_CAPABILITIES);
  if (capabilities == UINT32_MAX ||
      (capabilities & LPSS_CAPABILITIES_TYPE_MASK) != LPSS_CAPABILITIES_TYPE_I2C) {
    preparation_refused("LPSS capability is not I2C");
    return;
  }
  /* The native LPSS reset fences the function/APB and its internal DMA before
   * PCI completion clears BME. No transaction is issued to a bus client. */
  lpss.reset_started = true;
  write32(LPSS_RESETS, 0);
  if (read32(LPSS_RESETS) & LPSS_RESET_MASK) {
    preparation_refused("LPSS reset did not assert");
    return;
  }
  if (!pci_complete_claim(&lpss.claim)) {
    preparation_refused("PCI DMA/INTx disable");
    return;
  }
  uint16_t command = pci_read16(device->address, PCI_COMMAND);
  pci_write16(&lpss.claim, PCI_COMMAND, command & ~(PCI_COMMAND_IO | PCI_COMMAND_MEMORY));
  if (pci_read16(device->address, PCI_COMMAND) &
      (PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER)) {
    preparation_refused("PCI decode disable readback before BAR sizing");
    return;
  }
  if (!pci_size_bars(&lpss.claim) || lpss.claim.bars[0].bytes < LPSS_MAPPING_BYTES) {
    preparation_refused("sized BAR0 does not cover bootstrap window");
    return;
  }
  pci_write16(&lpss.claim, PCI_COMMAND, (command | PCI_COMMAND_MEMORY | PCI_COMMAND_INTX_DISABLE) &
      ~(PCI_COMMAND_IO | PCI_COMMAND_MASTER));
  if ((pci_read16(device->address, PCI_COMMAND) &
      (PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER | PCI_COMMAND_INTX_DISABLE)) !=
      (PCI_COMMAND_MEMORY | PCI_COMMAND_INTX_DISABLE)) {
    preparation_refused("PCI memory-decode readback");
    return;
  }
  write32(LPSS_RESETS, LPSS_FUNCTION_RUNNING);
  if ((read32(LPSS_RESETS) & LPSS_RESET_MASK) != LPSS_FUNCTION_RUNNING) {
    preparation_refused("LPSS function release/iDMA reset readback");
    return;
  }
  enum dw_i2c_result result = dw_i2c_identify(&lpss.master, lpss.mapping.address);
  kdebug("i2c-lpss: DesignWare type=0x%x version=0x%x parameters=0x%x identity=%s\n",
      lpss.master.component_type, lpss.master.component_version,
      lpss.master.component_parameters, dw_i2c_result_name(result));
  if (result != DW_I2C_OK) {
    preparation_refused("unsupported DesignWare identity/parameters");
    return;
  }
  result = dw_i2c_halt(&lpss.master, task_deadline_after_ms(LPSS_HANDOFF_TIMEOUT_MS));
  if (result != DW_I2C_OK) {
    kdebug("i2c-lpss: initial halt=%s status=0x%x abort=0x%x\n", dw_i2c_result_name(result),
        lpss.master.last_status, lpss.master.abort_source);
    preparation_refused("DesignWare idle/disable verification");
    return;
  }
  lpss.state = LPSS_PREPARED;
  kdebug("i2c-lpss: %u:%u.%u prepared; clock=%u Hz BAR0=0x%llx/%llu "
      "component=0x%x FIFO=%u/%u; DMA held reset\n", device->address.bus,
      device->address.device, device->address.function, LPSS_CLOCK_HZ,
      (unsigned long long)lpss.claim.bars[0].physical,
      (unsigned long long)lpss.claim.bars[0].bytes, lpss.master.component_version,
      lpss.master.tx_depth, lpss.master.rx_depth);
}

static uint32_t clock_cycles(uint32_t ns)
{
  return ((uint64_t)ns * LPSS_CLOCK_HZ + UINT64_C(999999999)) / UINT64_C(1000000000);
}

static bool get_timing(struct dw_i2c_timing *timing)
{
  uint32_t speed = lpss.endpoint.speed_hz;
  if (speed != 100000 && speed != 400000) {
    return false;
  }
  *timing = (struct dw_i2c_timing){.speed_hz = speed};
  if (lpss.companion.timing_present) {
    timing->high_count = lpss.companion.high_count;
    timing->low_count = lpss.companion.low_count;
    timing->sda_hold = lpss.companion.sda_hold;
  } else {
    /* This PCI identity uses the 100 MHz LPSS root without the UART/SPI
     * fractional divider. Round upward, including platform fall-time margins;
     * no subtraction of internal overhead that could exceed the requested rate. */
    timing->high_count = clock_cycles((speed == 100000 ? 4000 : 600) + 171);
    uint32_t low = clock_cycles((speed == 100000 ? 4700 : 1300) + 208);
    uint32_t period = (LPSS_CLOCK_HZ + speed - 1) / speed;
    timing->low_count = period - timing->high_count;
    if (timing->low_count < low) {
      timing->low_count = low;
    }
    timing->sda_hold = lpss.companion.hold_present ? lpss.companion.sda_hold : clock_cycles(42);
  }
  /* A zero RX hold can report false TX arbitration loss when the device
   * pulls SDA low quickly after SCL falls. Keep nonzero firmware RX timing. */
  if (!(timing->sda_hold & LPSS_SDA_RX_HOLD_MASK)) {
    timing->sda_hold |= LPSS_SDA_RX_HOLD_ONE;
  }
  return true;
}

static void descriptor_worker(void *argument)
{
  (void)argument;
  struct dw_i2c_timing timing;
  if (!get_timing(&timing)) {
    lpss.state = LPSS_REFUSED;
    kinfo("i2c-lpss: unsupported requested speed %u Hz\n", lpss.endpoint.speed_hz);
    return;
  }
  enum dw_i2c_result result = dw_i2c_configure(&lpss.master, &timing,
      task_deadline_after_ms(LPSS_HANDOFF_TIMEOUT_MS));
  if (result != DW_I2C_OK) {
    lpss.state = LPSS_REFUSED;
    kinfo("i2c-lpss: configuration refused: %s status=0x%x\n", dw_i2c_result_name(result),
        lpss.master.last_status);
    return;
  }
  kdebug("i2c-lpss: timing %s speed=%u HCNT=%u LCNT=%u SDA_HOLD=0x%x\n",
      lpss.companion.timing_present ? "firmware" : "platform", timing.speed_hz,
      timing.high_count, timing.low_count, timing.sda_hold);
  lpss.state = hid_i2c_descriptor_read(&lpss.master, &lpss.endpoint, &lpss.descriptor) ?
      LPSS_DESCRIPTOR_READY : LPSS_REFUSED;
}

void intel_lpss_i2c_attach(const struct acpi_hid_i2c_endpoint *endpoint,
    const struct acpi_hid_i2c_pci *companion)
{
  KASSERT(arch_cpu_index() == 0 && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  if (lpss.state != LPSS_PREPARED) {
    kinfo("i2c-lpss: firmware endpoint has no prepared controller (state=%u)\n", lpss.state);
    return;
  }
  struct pci_address prepared = lpss.claim.device->address;
  if (prepared.bus != companion->address.bus || prepared.device != companion->address.device ||
      prepared.function != companion->address.function) {
    kinfo("i2c-lpss: firmware companion %u:%u.%u differs from prepared %u:%u.%u; no transfer\n",
        companion->address.bus, companion->address.device, companion->address.function,
        prepared.bus, prepared.device, prepared.function);
    return;
  }
  lpss.endpoint = *endpoint;
  lpss.companion = *companion;
  lpss.state = LPSS_BOUND;
  enum mm_result result = kernel_task_create(descriptor_worker, NULL);
  if (result != MM_OK) {
    lpss.state = LPSS_REFUSED;
    kinfo("i2c-lpss: cannot create descriptor worker (error %u)\n", result);
  }
}
