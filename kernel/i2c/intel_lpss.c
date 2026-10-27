/* SPDX-License-Identifier: MPL-2.0 */
#include <arch/clock.h>
#include <arch/cpu.h>
#include <arch/pci.h>
#include <arch/smp.h>
#include <kernel/i2c/intel_lpss.h>
#include <kernel/i2c/designware.h>
#include <kernel/input/hid_i2c.h>
#include <kernel/log.h>
#include <kernel/mm/kernel_owner.h>
#include <kernel/panic.h>
#include <kernel/pci/registers.h>
#include <kernel/pci/assignment.h>
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
  LPSS_ASSIGNMENT_PENDING,
  LPSS_BOUND,
  LPSS_DESCRIPTOR_READY,
  LPSS_REFUSED,
};

/* Boot preparation and the sole BSP worker own this storage until reboot.
 * iDMA stays in reset; there are no DMA allocations or interrupt handlers. */
static struct {
  struct pci_claim claim;
  struct pci_mapping mapping;
  struct pci_assignment_window assignment_window;
  struct pci_probe_state probe;
  struct dw_i2c_master master;
  struct acpi_hid_i2c_endpoint endpoint;
  struct acpi_hid_i2c_pci companion;
  struct hid_i2c_descriptor descriptor;
  const struct gpio_input *interrupt;
  struct task_wait *descriptor_wait;
  enum lpss_state state;
  bool reset_started, assignment_pending;
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
  uint16_t pmcsr = lpss.probe.power_capability ?
      pci_read16(address, lpss.probe.power_capability + PCI_POWER_CONTROL) : 0;
  kinfo("i2c-lpss: %02x:%02x.%x preparation refused: %s command=0x%x original=0x%x "
      "pmcsr=0x%x original-pmcsr=0x%x\n",
      address.bus, address.device, address.function, reason, pci_read16(address, PCI_COMMAND),
      lpss.probe.command, pmcsr, lpss.probe.pmcsr);
  lpss.state = LPSS_REFUSED;
  if (!lpss.reset_started && !lpss.probe.d3hot_handoff &&
      !lpss.claim.assignment_pinned && lpss.claim.reserved) {
    if (lpss.mapping.base) {
      pci_unmap(&lpss.claim, &lpss.mapping);
    }
    if (pci_restore_mmio_probe(&lpss.claim, &lpss.probe)) {
      pci_cancel_reservation(&lpss.claim);
    }
  }
  /* After D3hot handoff or reset, no assumed firmware-state restore or
   * speculative release. The still-owned resources stay until reboot. */
}

static void initialize_controller(bool assigned)
{
  struct pci_device *device = lpss.claim.device;
  uint32_t capabilities = read32(LPSS_CAPABILITIES);
  if (capabilities == UINT32_MAX ||
      (capabilities & LPSS_CAPABILITIES_TYPE_MASK) != LPSS_CAPABILITIES_TYPE_I2C) {
    preparation_refused("LPSS capability is not I2C");
    return;
  }
  /* The preassigned path resets before PCI completion clears BME. The placed
   * path already owns the D3hot fence. No transaction is issued to a client. */
  lpss.reset_started = true;
  write32(LPSS_RESETS, 0);
  if (read32(LPSS_RESETS) & LPSS_RESET_MASK) {
    preparation_refused("LPSS reset did not assert");
    return;
  }
  if (!assigned) {
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
  kdebug("i2c-lpss: %02x:%02x.%x prepared; clock=%u Hz BAR0=0x%llx/%llu "
      "component=0x%x FIFO=%u/%u; DMA held reset\n", device->address.bus,
      device->address.device, device->address.function, LPSS_CLOCK_HZ,
      (unsigned long long)lpss.claim.bars[0].physical,
      (unsigned long long)lpss.claim.bars[0].bytes, lpss.master.component_version,
      lpss.master.tx_depth, lpss.master.rx_depth);
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
  uint32_t bar0 = pci_read32(device->address, PCI_BAR_FIRST);
  unsigned type = bar0 & PCI_BAR_MEMORY_TYPE_MASK;
  bool unassigned = !(bar0 & PCI_BAR_IO) && !(bar0 & PCI_BAR_MEMORY_ADDRESS_MASK) &&
      (type == PCI_BAR_MEMORY_32 || (type == PCI_BAR_MEMORY_64 &&
       !pci_read32(device->address, PCI_BAR_FIRST + PCI_REGISTER_BYTES)));
  if (unassigned) {
    if (!pci_begin_d3hot_assignment_probe(&lpss.claim, &lpss.probe)) {
      preparation_refused(lpss.probe.refusal);
      return;
    }
    if (pci_assignment_window_prepare(&lpss.claim, LPSS_MAPPING_BYTES,
        &lpss.assignment_window) != MM_OK) {
      preparation_refused("native assignment window reservation");
      return;
    }
    lpss.assignment_pending = true;
    lpss.state = LPSS_ASSIGNMENT_PENDING;
    kdebug("i2c-lpss: unassigned BAR0 fenced in D0; awaiting live PCI resources/companion\n");
    return;
  }
  if (!pci_begin_d3hot_mmio_probe(&lpss.claim, &lpss.probe)) {
    preparation_refused(lpss.probe.refusal);
    return;
  }
  if (pci_map_bootstrap_bar(&lpss.claim, 0, 0, LPSS_MAPPING_BYTES, boot,
      &lpss.mapping) != MM_OK) {
    preparation_refused("BAR0 bootstrap mapping");
    return;
  }
  initialize_controller(false);
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
  /* BSP IF=0 joins the state check/park with ACPI's endpoint publication. */
  uint64_t wait_flags = cpu_save_interrupts();
  while (lpss.state != LPSS_BOUND) {
    lpss.descriptor_wait = task_wait_prepare();
    task_wait_sleep(lpss.descriptor_wait);
    lpss.descriptor_wait = NULL;
  }
  cpu_restore_interrupts(wait_flags);
  if (lpss.assignment_pending) {
    /* Keep the whole pending preparation across admission, before any attempt
     * record or hardware effect. Contention cannot declare it unsupported. */
    struct vm_kernel_owner owner = {0};
    vm_kernel_owner_cleanup(&owner);
    uint64_t flags = cpu_save_interrupts();
    uintptr_t address;
    if (!pci_assign_memory_bar(&lpss.claim, 0, &lpss.assignment_window, &address)) {
      preparation_refused("PCI memory assignment; see resource refusal");
      vm_kernel_owner_release(&owner);
      cpu_restore_interrupts(flags);
      return;
    }
    if (lpss.claim.bars[0].bytes < LPSS_MAPPING_BYTES) {
      preparation_refused("assigned BAR0 is smaller than the LPSS register extent");
      vm_kernel_owner_release(&owner);
      cpu_restore_interrupts(flags);
      return;
    }
    /* The earlier verified D3hot/decode/BME fence is this path's ownership
     * proof. Complete it before enabling MMIO; iDMA is never enabled. */
    if (!pci_complete_claim(&lpss.claim)) {
      preparation_refused("assigned PCI claim completion");
      vm_kernel_owner_release(&owner);
      cpu_restore_interrupts(flags);
      return;
    }
    struct pci_address pci = lpss.claim.device->address;
    uint16_t command = pci_read16(pci, PCI_COMMAND);
    pci_write16(&lpss.claim, PCI_COMMAND,
        (command | PCI_COMMAND_MEMORY | PCI_COMMAND_INTX_DISABLE) &
        ~(PCI_COMMAND_MASTER | PCI_COMMAND_IO));
    if ((pci_read16(pci, PCI_COMMAND) & (PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER |
        PCI_COMMAND_IO | PCI_COMMAND_INTX_DISABLE)) !=
        (PCI_COMMAND_MEMORY | PCI_COMMAND_INTX_DISABLE)) {
      preparation_refused("assigned PCI memory-decode enable readback");
      vm_kernel_owner_release(&owner);
      cpu_restore_interrupts(flags);
      return;
    }
    lpss.mapping.address = address;
    vm_kernel_owner_release(&owner);
    initialize_controller(true);
    cpu_restore_interrupts(flags);
    if (lpss.state != LPSS_PREPARED) {
      return;
    }
    lpss.assignment_pending = false;
    lpss.state = LPSS_BOUND;
  }
  struct dw_i2c_timing timing;
  if (!get_timing(&timing)) {
    lpss.state = LPSS_REFUSED;
    kinfo("i2c-lpss: unsupported requested speed %u Hz\n", lpss.endpoint.speed_hz);
    return;
  }
  kdebug("i2c-lpss: timing %s speed=%u HCNT=%u LCNT=%u SDA_HOLD=0x%x\n",
      lpss.companion.timing_present ? "firmware" : "platform", timing.speed_hz,
      timing.high_count, timing.low_count, timing.sda_hold);
  enum dw_i2c_result result = dw_i2c_configure(&lpss.master, &timing,
      task_deadline_after_ms(LPSS_HANDOFF_TIMEOUT_MS));
  if (result != DW_I2C_OK) {
    lpss.state = LPSS_REFUSED;
    kinfo("i2c-lpss: configuration refused: %s step=%s status=0x%x\n",
        dw_i2c_result_name(result), lpss.master.last_step, lpss.master.last_status);
    if (lpss.master.readback.mask) {
      kinfo("i2c-lpss: readback offset=0x%x expected=0x%x actual=0x%x mask=0x%x\n",
          lpss.master.readback.offset, lpss.master.readback.expected,
          lpss.master.readback.actual, lpss.master.readback.mask);
    }
    return;
  }
  lpss.state = hid_i2c_descriptor_read(&lpss.master, &lpss.endpoint, &lpss.descriptor) ?
      LPSS_DESCRIPTOR_READY : LPSS_REFUSED;
  if (lpss.state == LPSS_DESCRIPTOR_READY) {
    hid_i2c_poll(&lpss.master, &lpss.endpoint, &lpss.descriptor, lpss.interrupt);
  }
}

void intel_lpss_i2c_start(void)
{
  KASSERT(arch_cpu_index() == 0 && !cpu_interrupts_enabled() && !vm_kernel_runtime());
  if (lpss.state != LPSS_PREPARED && lpss.state != LPSS_ASSIGNMENT_PENDING) {
    return;
  }
  enum mm_result result = kernel_task_create(descriptor_worker, NULL);
  if (result != MM_OK) {
    lpss.state = LPSS_REFUSED;
    kinfo("i2c-lpss: cannot create descriptor worker (error %u)\n", result);
  }
}

void intel_lpss_i2c_attach(const struct acpi_hid_i2c_endpoint *endpoint,
    const struct acpi_hid_i2c_pci *companion, const struct gpio_input *interrupt)
{
  KASSERT(arch_cpu_index() == 0 && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  if (lpss.state != LPSS_PREPARED && lpss.state != LPSS_ASSIGNMENT_PENDING) {
    kinfo("i2c-lpss: firmware endpoint has no prepared controller (state=%u)\n", lpss.state);
    return;
  }
  struct pci_address prepared = lpss.claim.device->address;
  if (prepared.bus != companion->address.bus || prepared.device != companion->address.device ||
      prepared.function != companion->address.function) {
    kinfo("i2c-lpss: firmware companion %02x:%02x.%x differs from prepared %02x:%02x.%x; no transfer\n",
        companion->address.bus, companion->address.device, companion->address.function,
        prepared.bus, prepared.device, prepared.function);
    return;
  }
  lpss.endpoint = *endpoint;
  lpss.companion = *companion;
  lpss.interrupt = interrupt;
  lpss.state = LPSS_BOUND;
  struct task_wait *wait = lpss.descriptor_wait;
  lpss.descriptor_wait = NULL;
  if (wait) {
    task_wait_wake(wait);
  }
}
