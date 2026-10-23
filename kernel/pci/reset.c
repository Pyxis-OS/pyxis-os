#include <arch/clock.h>
#include <arch/cpu.h>
#include <arch/cpu_local.h>
#include <arch/pci.h>
#include <arch/smp.h>
#include <kernel/panic.h>
#include <kernel/pci.h>
#include <kernel/pci/registers.h>
#include <kernel/task.h>

#define RESET_FLR_WAIT_MS 100u
#define RESET_POWER_WAIT_MS 10u
#define RESET_POLL_MS 1u
#define RESET_NS_PER_MS UINT64_C(1000000)
#define RESET_ROM_ADDRESS_MASK UINT32_C(0xfffff800)

static void
require_owner(const struct pci_function_reset *reset)
{
  KASSERT(cpu_current() == cpu_bsp() && !cpu_interrupts_enabled());
  KASSERT(reset && reset->claim && reset->claim->device &&
      reset->claim->device->owner == reset->claim &&
      reset->claim->generation == reset->generation);
}

static bool
refuse(struct pci_function_reset *reset, const char *detail)
{
  reset->detail = detail;
  return false;
}

static bool
identity_valid(const struct pci_function_reset *reset)
{
  const struct pci_device *device = reset->claim->device;
  struct pci_address address = device->address;
  return pci_read16(address, PCI_VENDOR_ID) == device->vendor_id &&
      pci_read16(address, PCI_DEVICE_ID) == device->device_id &&
      pci_read8(address, PCI_REVISION) == device->revision &&
      pci_read8(address, PCI_INTERFACE) == device->interface &&
      pci_read8(address, PCI_SUBCLASS) == device->subclass &&
      pci_read8(address, PCI_CLASS) == device->base_class &&
      (pci_read8(address, PCI_HEADER_TYPE) & PCI_HEADER_TYPE_MASK) == PCI_HEADER_ENDPOINT;
}

static bool
capabilities_valid(const struct pci_function_reset *reset)
{
  const struct pci_claim *claim = reset->claim;
  struct pci_address address = claim->device->address;
  if (claim->capability_count != reset->capability_count ||
      !(pci_read16(address, PCI_STATUS) & PCI_STATUS_CAPABILITIES))
    return false;
  unsigned next = pci_read8(address, PCI_CAPABILITIES) & PCI_CAP_POINTER_MASK;
  for (unsigned i = 0; i < reset->capability_count; ++i) {
    unsigned offset = claim->capabilities[i];
    if (next != offset || pci_read16(address, offset) != reset->capability_headers[i])
      return false;
    next = reset->capability_headers[i] >> 8;
    next &= PCI_CAP_POINTER_MASK;
  }
  return !next && pci_read16(address, reset->express_capability + PCI_EXPRESS_FLAGS) ==
      reset->express_flags &&
      pci_read32(address, reset->express_capability + PCI_EXPRESS_DEVICE_CAPABILITIES) ==
      reset->device_capabilities;
}

static bool
discover_capabilities(struct pci_function_reset *reset)
{
  struct pci_claim *claim = reset->claim;
  struct pci_address address = claim->device->address;
  if (!claim->capability_count || claim->capability_count > PCI_CAP_COUNT)
    return false;
  reset->capability_count = claim->capability_count;
  for (unsigned i = 0; i < claim->capability_count; ++i) {
    unsigned offset = claim->capabilities[i];
    uint16_t header = pci_read16(address, offset);
    reset->capability_headers[i] = header;
    unsigned id = header & UINT8_MAX;
    unsigned *selected = NULL;
    size_t bytes = 0;
    if (id == PCI_CAP_EXPRESS) {
      selected = &reset->express_capability;
      bytes = PCI_EXPRESS_LINK_BYTES;
    } else if (id == PCI_CAP_POWER) {
      selected = &reset->power_capability;
      bytes = PCI_POWER_BYTES;
    } else if (id == PCI_CAP_MSI) {
      selected = &reset->msi_capability;
      bytes = PCI_MSI_CONTROL + sizeof(uint16_t);
    } else if (id == PCI_CAP_MSIX) {
      selected = &reset->msix_capability;
      bytes = PCI_MSIX_BYTES;
    }
    if (header == UINT16_MAX || (selected &&
        (*selected || !pci_capability_fits(claim, offset, bytes))))
      return false;
    if (selected)
      *selected = offset;
  }
  if (!reset->express_capability)
    return false;
  reset->express_flags = pci_read16(address, reset->express_capability + PCI_EXPRESS_FLAGS);
  unsigned version = reset->express_flags & PCI_EXPRESS_VERSION_MASK;
  unsigned type = reset->express_flags & PCI_EXPRESS_TYPE_MASK;
  if ((version != 1 && version != 2) ||
      (type != PCI_EXPRESS_ENDPOINT && type != PCI_EXPRESS_LEGACY_ENDPOINT) ||
      (version == 2 && !pci_capability_fits(claim, reset->express_capability,
          PCI_EXPRESS_V2_BYTES)))
    return false;
  if (reset->power_capability) {
    unsigned power_version = pci_read16(address,
        reset->power_capability + PCI_POWER_CAPABILITIES) & PCI_POWER_VERSION_MASK;
    if (!power_version || power_version > 3)
      return false;
  }
  reset->device_capabilities = pci_read32(address,
      reset->express_capability + PCI_EXPRESS_DEVICE_CAPABILITIES);
  return reset->device_capabilities != UINT32_MAX && capabilities_valid(reset);
}

static bool
capture_configuration(struct pci_function_reset *reset)
{
  struct pci_address address = reset->claim->device->address;
  if (!identity_valid(reset) || !capabilities_valid(reset))
    return refuse(reset, "PCI reset identity/capability validation failed");
  reset->command = pci_read16(address, PCI_COMMAND);
  reset->power_control = reset->power_capability ?
      pci_read16(address, reset->power_capability + PCI_POWER_CONTROL) : PCI_POWER_D0;
  unsigned power = reset->power_control & PCI_POWER_STATE_MASK;
  if (reset->command == UINT16_MAX || reset->power_control == UINT16_MAX ||
      (power != PCI_POWER_D0 && power != PCI_POWER_D3HOT))
    return refuse(reset, "PCI reset command or power state unsupported");
  if (reset->method == PCI_RESET_POWER &&
      (reset->power_control & PCI_POWER_NO_SOFT_RESET))
    return refuse(reset, "PCI PM reset requires NoSoftReset clear");
  for (unsigned i = 0; i < PCI_BAR_COUNT; ++i) {
    reset->bar_values[i] = pci_read32(address, PCI_BAR_FIRST + i * PCI_REGISTER_BYTES);
    if (reset->bar_values[i] == UINT32_MAX)
      return refuse(reset, "PCI reset BAR snapshot inaccessible");
  }
  uint32_t expansion_rom = pci_read32(address, PCI_EXPANSION_ROM);
  if (expansion_rom == UINT32_MAX)
    return refuse(reset, "PCI reset ROM snapshot inaccessible");
  reset->expansion_rom = expansion_rom & RESET_ROM_ADDRESS_MASK;
  reset->subsystem = pci_read32(address, PCI_SUBSYSTEM_VENDOR);
  reset->cache_line = pci_read8(address, PCI_CACHE_LINE);
  reset->latency = pci_read8(address, PCI_LATENCY);
  reset->interrupt_line = pci_read8(address, PCI_INTERRUPT_LINE);
  reset->device_control = pci_read16(address,
      reset->express_capability + PCI_EXPRESS_DEVICE_CONTROL);
  reset->link_control = pci_read16(address,
      reset->express_capability + PCI_EXPRESS_LINK_CONTROL);
  if ((reset->express_flags & PCI_EXPRESS_VERSION_MASK) == 2)
    reset->device_control2 = pci_read16(address,
        reset->express_capability + PCI_EXPRESS_DEVICE_CONTROL2);
  if (reset->device_control == UINT16_MAX || reset->link_control == UINT16_MAX ||
      (reset->device_control & PCI_EXPRESS_INITIATE_FLR) ||
      reset->device_control2 == UINT16_MAX)
    return refuse(reset, "PCI reset control snapshot unsupported");
  return true;
}

bool
pci_function_reset_prepare(struct pci_claim *claim, struct pci_function_reset *reset)
{
  KASSERT(cpu_current() == cpu_bsp() && !cpu_interrupts_enabled() && !arch_cpu_count());
  KASSERT(claim && claim->device && claim->device->owner == claim && claim->reserved &&
      !claim->mappings && !claim->dma_started && !claim->reset_retained &&
      reset && !reset->claim);
  *reset = (struct pci_function_reset){.claim = claim, .generation = claim->generation};
  if (!identity_valid(reset) || !discover_capabilities(reset))
    return refuse(reset, "PCI reset endpoint/capability profile unsupported");
  if (reset->device_capabilities & PCI_EXPRESS_FLR_SUPPORTED)
    reset->method = PCI_RESET_FLR;
  else if (reset->power_capability)
    reset->method = PCI_RESET_POWER;
  else
    return refuse(reset, "PCI endpoint advertises neither FLR nor PM reset");
  if (!capture_configuration(reset))
    return false;
  reset->prepared = true;
  reset->detail = reset->method == PCI_RESET_FLR ? "PCI FLR selected from live DevCap" :
      "PCI resetting D3hot to D0 selected from live PMCSR";
  return true;
}

static bool
fence_configuration(struct pci_function_reset *reset, bool already_fenced)
{
  uint64_t flags = cpu_save_interrupts();
  require_owner(reset);
  struct pci_address address = reset->claim->device->address;
  uint16_t command = pci_read16(address, PCI_COMMAND);
  bool valid = command != UINT16_MAX && identity_valid(reset) && capabilities_valid(reset);
  if (valid) {
    bool disabled = !(command & (PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER));
    reset->claim->reset_retained = true;
    command = (command & ~(PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER)) |
        PCI_COMMAND_INTX_DISABLE;
    arch_pci_write16(address, PCI_COMMAND, command);
    valid = pci_read16(address, PCI_COMMAND) == command && (!already_fenced || disabled);
    if (reset->msi_capability) {
      unsigned offset = reset->msi_capability + PCI_MSI_CONTROL;
      uint16_t control = pci_read16(address, offset);
      if (control == UINT16_MAX) {
        valid = false;
      } else {
        control &= ~(PCI_MSI_ENABLE | PCI_MSI_MESSAGE_ENABLE_MASK);
        arch_pci_write16(address, offset, control);
        valid = pci_read16(address, offset) == control && valid;
      }
    }
    if (reset->msix_capability) {
      unsigned offset = reset->msix_capability + PCI_MSIX_CONTROL;
      uint16_t control = pci_read16(address, offset);
      if (control == UINT16_MAX) {
        valid = false;
      } else {
        control = (control & ~PCI_MSIX_ENABLE) | PCI_MSIX_FUNCTION_MASK;
        arch_pci_write16(address, offset, control);
        valid = pci_read16(address, offset) == control && valid;
      }
    }
  }
  cpu_restore_interrupts(flags);
  return valid || refuse(reset, "PCI reset decode/DMA/interrupt fence failed; retained");
}

static void
wait_ms(unsigned milliseconds, bool worker)
{
  uint64_t deadline = task_deadline_after_ms(milliseconds);
  do {
    if (worker) {
      kernel_task_sleep_until(deadline);
    } else {
      arch_clock_maintain();
      cpu_relax();
    }
  } while (!task_deadline_expired(deadline));
}

static bool
time_available(uint64_t deadline, unsigned milliseconds)
{
  uint64_t now = arch_monotonic_ns();
  uint64_t interval = (uint64_t)milliseconds * RESET_NS_PER_MS;
  return now < deadline && interval <= deadline - now;
}

static bool
wait_ready(struct pci_function_reset *reset, uint64_t deadline, bool worker)
{
  struct pci_address address = reset->claim->device->address;
  while (!task_deadline_expired(deadline)) {
    uint16_t vendor = pci_read16(address, PCI_VENDOR_ID);
    uint16_t command = pci_read16(address, PCI_COMMAND);
    /* Vendor 0001 is Configuration Request Retry Status, not a new device. */
    if (vendor != PCI_NO_VENDOR && vendor != 1 && command != UINT16_MAX) {
      if (!identity_valid(reset) || !capabilities_valid(reset))
        return refuse(reset, "PCI reset returned changed identity/capabilities; retained");
      uint16_t control = pci_read16(address,
          reset->express_capability + PCI_EXPRESS_DEVICE_CONTROL);
      if (control != UINT16_MAX && !(control & PCI_EXPRESS_INITIATE_FLR))
        return true;
    }
    wait_ms(RESET_POLL_MS, worker);
  }
  return refuse(reset, "PCI reset configuration readiness timeout; retained");
}

static bool
power_transition(struct pci_function_reset *reset, unsigned power,
    uint64_t deadline, bool worker)
{
  if (!time_available(deadline, RESET_POWER_WAIT_MS))
    return refuse(reset, "PCI reset power-transition deadline; retained");
  uint64_t flags = cpu_save_interrupts();
  require_owner(reset);
  struct pci_address address = reset->claim->device->address;
  unsigned offset = reset->power_capability + PCI_POWER_CONTROL;
  uint16_t control = pci_read16(address, offset);
  if (control == UINT16_MAX) {
    cpu_restore_interrupts(flags);
    return refuse(reset, "PCI reset PMCSR inaccessible; retained");
  }
  /* PME status is W1C. Preserve enable/data selection and write status zero. */
  uint16_t expected = (reset->power_control &
      (PCI_POWER_CONTROL_WRITABLE & ~PCI_POWER_STATE_MASK)) | power;
  arch_pci_write16(address, offset, expected);
  cpu_restore_interrupts(flags);
  wait_ms(RESET_POWER_WAIT_MS, worker);
  if (power == PCI_POWER_D0 && !wait_ready(reset, deadline, worker))
    return false;
  control = pci_read16(address, offset);
  /* PME/Data_Select may survive or reset on a soft reset. Restore/check them
   * with the configuration snapshot, not as a transition-completion condition. */
  if (control == UINT16_MAX ||
      (control & PCI_POWER_STATE_MASK) != power ||
      (control & PCI_POWER_NO_SOFT_RESET) != (reset->power_control & PCI_POWER_NO_SOFT_RESET))
    return refuse(reset, "PCI reset power-transition readback failed; retained");
  return true;
}

static bool
wait_transactions(struct pci_function_reset *reset, uint64_t deadline, bool worker,
    unsigned minimum_wait_ms)
{
  struct pci_address address = reset->claim->device->address;
  while (time_available(deadline, minimum_wait_ms)) {
    uint16_t status = pci_read16(address, reset->express_capability + PCI_EXPRESS_DEVICE_STATUS);
    if (status == UINT16_MAX)
      return refuse(reset, "PCI reset transaction status inaccessible; retained");
    if (!(status & PCI_EXPRESS_TRANSACTION_PENDING))
      return true;
    wait_ms(RESET_POLL_MS, worker);
  }
  return refuse(reset, "PCI reset pending transactions/deadline refused; retained");
}

static bool
perform_flr(struct pci_function_reset *reset, uint64_t deadline, bool worker)
{
  if (!wait_transactions(reset, deadline, worker, RESET_FLR_WAIT_MS))
    return false;
  uint64_t flags = cpu_save_interrupts();
  require_owner(reset);
  struct pci_address address = reset->claim->device->address;
  unsigned offset = reset->express_capability + PCI_EXPRESS_DEVICE_CONTROL;
  uint16_t control = pci_read16(address, offset);
  if (control == UINT16_MAX || (control & PCI_EXPRESS_INITIATE_FLR)) {
    cpu_restore_interrupts(flags);
    return refuse(reset, "PCI FLR control inaccessible or already resetting; retained");
  }
  arch_pci_write16(address, offset, control | PCI_EXPRESS_INITIATE_FLR);
  cpu_restore_interrupts(flags);
  /* Requests may be discarded during FLR; do not read configuration yet. */
  wait_ms(RESET_FLR_WAIT_MS, worker);
  return wait_ready(reset, deadline, worker);
}

static bool
restore_configuration(struct pci_function_reset *reset)
{
  if (!fence_configuration(reset, true))
    return false;
  uint64_t flags = cpu_save_interrupts();
  require_owner(reset);
  struct pci_address address = reset->claim->device->address;
  uint16_t expected_power = reset->power_control &
      (PCI_POWER_CONTROL_WRITABLE & ~PCI_POWER_STATE_MASK);
  if (reset->power_capability) {
    uint16_t power = pci_read16(address, reset->power_capability + PCI_POWER_CONTROL);
    if (power == UINT16_MAX || (power & PCI_POWER_STATE_MASK) != PCI_POWER_D0 ||
        (power & PCI_POWER_NO_SOFT_RESET) != (reset->power_control & PCI_POWER_NO_SOFT_RESET)) {
      cpu_restore_interrupts(flags);
      return refuse(reset, "PCI reset did not retain usable D0 power; retained");
    }
    arch_pci_write16(address, reset->power_capability + PCI_POWER_CONTROL, expected_power);
  }
  for (unsigned i = 0; i < PCI_BAR_COUNT; ++i)
    arch_pci_write32(address, PCI_BAR_FIRST + i * PCI_REGISTER_BYTES, reset->bar_values[i]);
  /* Restore the assigned ROM address without enabling its decoder. */
  arch_pci_write32(address, PCI_EXPANSION_ROM, reset->expansion_rom);
  arch_pci_write8(address, PCI_CACHE_LINE, reset->cache_line);
  arch_pci_write8(address, PCI_LATENCY, reset->latency);
  arch_pci_write8(address, PCI_INTERRUPT_LINE, reset->interrupt_line);
  arch_pci_write16(address, reset->express_capability + PCI_EXPRESS_DEVICE_CONTROL,
      reset->device_control);
  arch_pci_write16(address, reset->express_capability + PCI_EXPRESS_LINK_CONTROL,
      reset->link_control);
  if ((reset->express_flags & PCI_EXPRESS_VERSION_MASK) == 2)
    arch_pci_write16(address, reset->express_capability + PCI_EXPRESS_DEVICE_CONTROL2,
        reset->device_control2);
  uint16_t command = (reset->command &
      ~(PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER)) | PCI_COMMAND_INTX_DISABLE;
  arch_pci_write16(address, PCI_COMMAND, command);
  bool valid = identity_valid(reset) && capabilities_valid(reset) &&
      pci_read16(address, PCI_COMMAND) == command &&
      pci_read32(address, PCI_SUBSYSTEM_VENDOR) == reset->subsystem &&
      pci_read32(address, PCI_EXPANSION_ROM) == reset->expansion_rom &&
      pci_read8(address, PCI_CACHE_LINE) == reset->cache_line &&
      pci_read8(address, PCI_LATENCY) == reset->latency &&
      pci_read8(address, PCI_INTERRUPT_LINE) == reset->interrupt_line &&
      pci_read16(address, reset->express_capability + PCI_EXPRESS_DEVICE_CONTROL) ==
          reset->device_control &&
      pci_read16(address, reset->express_capability + PCI_EXPRESS_LINK_CONTROL) ==
          reset->link_control;
  if ((reset->express_flags & PCI_EXPRESS_VERSION_MASK) == 2)
    valid = pci_read16(address, reset->express_capability + PCI_EXPRESS_DEVICE_CONTROL2) ==
        reset->device_control2 && valid;
  for (unsigned i = 0; i < PCI_BAR_COUNT; ++i)
    valid = pci_read32(address, PCI_BAR_FIRST + i * PCI_REGISTER_BYTES) ==
        reset->bar_values[i] && valid;
  if (reset->power_capability)
    valid = (pci_read16(address, reset->power_capability + PCI_POWER_CONTROL) &
        PCI_POWER_CONTROL_WRITABLE) == expected_power && valid;
  if (reset->msi_capability)
    valid = !(pci_read16(address, reset->msi_capability + PCI_MSI_CONTROL) &
        (PCI_MSI_ENABLE | PCI_MSI_MESSAGE_ENABLE_MASK)) && valid;
  if (reset->msix_capability)
    valid = (pci_read16(address, reset->msix_capability + PCI_MSIX_CONTROL) &
        (PCI_MSIX_ENABLE | PCI_MSIX_FUNCTION_MASK)) == PCI_MSIX_FUNCTION_MASK && valid;
  cpu_restore_interrupts(flags);
  return valid || refuse(reset, "PCI reset configuration restore failed; retained");
}

static bool
execute_reset(struct pci_function_reset *reset, uint64_t deadline, bool worker)
{
  if (!fence_configuration(reset, false))
    return false;
  bool initially_d3hot = (reset->power_control & PCI_POWER_STATE_MASK) == PCI_POWER_D3HOT;
  reset->power_normalized = false;
  reset->normalization_reset = false;
  if (initially_d3hot) {
    unsigned minimum_wait = 2 * RESET_POWER_WAIT_MS +
        (reset->method == PCI_RESET_FLR ? RESET_FLR_WAIT_MS : 0);
    if (!wait_transactions(reset, deadline, worker, minimum_wait))
      return false;
    /* An inherited D3hot observation does not establish its residence time. */
    wait_ms(RESET_POWER_WAIT_MS, worker);
    if (!power_transition(reset, PCI_POWER_D0, deadline, worker))
      return false;
    reset->power_normalized = true;
    reset->normalization_reset = !(reset->power_control & PCI_POWER_NO_SOFT_RESET);
    if (!fence_configuration(reset, true))
      return false;
  }
  if (reset->method == PCI_RESET_FLR) {
    if (!perform_flr(reset, deadline, worker))
      return false;
  } else if (!initially_d3hot) {
    if (!wait_transactions(reset, deadline, worker, 2 * RESET_POWER_WAIT_MS) ||
        !power_transition(reset, PCI_POWER_D3HOT, deadline, worker) ||
        !power_transition(reset, PCI_POWER_D0, deadline, worker))
      return false;
  }
  return restore_configuration(reset);
}

bool
pci_function_reset_boot(struct pci_function_reset *reset, uint64_t deadline_ns)
{
  require_owner(reset);
  KASSERT(!arch_cpu_count() && reset->claim->reserved && !reset->claim->mappings &&
      !reset->claim->dma_started && !reset->running);
  if (!reset->prepared || reset->boot_attempted)
    return refuse(reset, "PCI boot reset unavailable or already attempted");
  reset->boot_attempted = true;
  if (deadline_ns == UINT64_MAX)
    return refuse(reset, "PCI boot reset requires a finite deadline");
  reset->running = true;
  bool valid = capture_configuration(reset) && execute_reset(reset, deadline_ns, false);
  reset->running = false;
  reset->boot_completed = valid;
  if (valid)
    reset->detail = "PCI boot reset complete; decoding/DMA/interrupts disabled";
  return valid;
}

bool
pci_function_reset_stop(struct pci_function_reset *reset, uint64_t deadline_ns)
{
  KASSERT(cpu_current() == cpu_bsp() && cpu_interrupts_enabled());
  uint64_t flags = cpu_save_interrupts();
  require_owner(reset);
  KASSERT(!reset->claim->reserved && !reset->running);
  if (!reset->boot_completed || reset->stop_attempted) {
    cpu_restore_interrupts(flags);
    return refuse(reset, "PCI stop reset unavailable or already attempted");
  }
  reset->stop_attempted = true;
  if (deadline_ns == UINT64_MAX) {
    cpu_restore_interrupts(flags);
    return refuse(reset, "PCI stop reset requires a finite deadline");
  }
  reset->running = true;
  bool captured = capture_configuration(reset);
  cpu_restore_interrupts(flags);
  bool valid = captured && execute_reset(reset, deadline_ns, true);
  flags = cpu_save_interrupts();
  reset->running = false;
  reset->stop_completed = valid;
  if (valid)
    reset->detail = "PCI stop reset complete; resources retained until reboot";
  cpu_restore_interrupts(flags);
  return valid;
}
