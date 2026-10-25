#include "internal.h"

#include <arch/apic.h>
#include <arch/clock.h>
#include <arch/cpu.h>
#include <arch/cpu_local.h>
#include <arch/dma.h>
#include <arch/pci.h>
#include <kernel/log.h>
#include <kernel/memory.h>
#include <kernel/panic.h>
#include <kernel/pci/registers.h>
#include <kernel/task.h>

#define HDA_INTEL_VENDOR 0x8086
#define HDA_QEMU_DEVICE 0x2668
#define HDA_INTEL_PTL_H_DEVICE 0xe328
#define HDA_AMD_VENDOR 0x1022
#define HDA_PCI_CLASS 0x04
#define HDA_PCI_SUBCLASS 0x03
#define HDA_PCI_AUDIO_SUBCLASS 0x01
#define HDA_PCI_INTERFACE 0x00
#define HDA_PCI_DISPLAY_CLASS 0x03
#define HDA_INTEL_TCSEL 0x44
#define HDA_INTEL_TCSEL_MASK 0x07
#define HDA_INTEL_CGCTL 0x48
#define HDA_INTEL_CGCTL_MISCBDCGE (1u << 6)
#define HDA_INTEL_DEVC 0x78
#define HDA_INTEL_DEVC_NO_SNOOP (1u << 11)
#define HDA_PCIE_FLAGS 0x02
#define HDA_PCIE_VERSION_MASK 0x0f
#define HDA_PCIE_TYPE_SHIFT 4
#define HDA_PCIE_TYPE_MASK 0x0f
#define HDA_PCIE_ENDPOINT 0
#define HDA_PCIE_ROOT_ENDPOINT 9
#define HDA_PCIE_SLOT_IMPLEMENTED (1u << 8)
#define HDA_PCIE_RESERVED_FLAGS (1u << 14)
#define HDA_PCIE_DEVICE_CONTROL 0x08
#define HDA_PCIE_DEVICE_CONTROL_BYTES 0x0a
#define HDA_PCIE_NO_SNOOP (1u << 11)
#define HDA_PCIE_FUNCTION_RESET (1u << 15)
#define HDA_DMA_MAX_32 UINT64_C(0xffffffff)
#define HDA_DMA_MAX_40 ((UINT64_C(1) << 40) - 1)
#define HDA_VERSION_MAJOR 1

#define HDA_GCAP 0x00
#define HDA_VMIN 0x02
#define HDA_VMAJ 0x03
#define HDA_GCTL 0x08
#define HDA_WAKEEN 0x0c
#define HDA_STATESTS 0x0e
#define HDA_INTCTL 0x20
#define HDA_INTSTS 0x24
#define HDA_WALCLK 0x30
#define HDA_CORBLBASE 0x40
#define HDA_CORBUBASE 0x44
#define HDA_CORBWP 0x48
#define HDA_CORBRP 0x4a
#define HDA_CORBCTL 0x4c
#define HDA_CORBSTS 0x4d
#define HDA_CORBSIZE 0x4e
#define HDA_RIRBLBASE 0x50
#define HDA_RIRBUBASE 0x54
#define HDA_RIRBWP 0x58
#define HDA_RINTCNT 0x5a
#define HDA_RIRBCTL 0x5c
#define HDA_RIRBSTS 0x5d
#define HDA_RIRBSIZE 0x5e
#define HDA_IC 0x60
#define HDA_IR 0x64
#define HDA_IRS 0x68
#define HDA_LLCH 0x14
#define HDA_DPLBASE 0x70
#define HDA_DPUBASE 0x74
#define HDA_STREAM_BASE 0x80
#define HDA_STREAM_BYTES 0x20
#define HDA_SD_CTL 0x00
#define HDA_SD_STS 0x03
#define HDA_SD_LPIB 0x04
#define HDA_SD_CBL 0x08
#define HDA_SD_LVI 0x0c
#define HDA_SD_FIFOS 0x10
#define HDA_SD_FMT 0x12
#define HDA_SD_BDPL 0x18
#define HDA_SD_BDPU 0x1c

#define HDA_GCAP_64OK (1u << 0)
#define HDA_GCAP_BSS_SHIFT 3
#define HDA_GCAP_ISS_SHIFT 8
#define HDA_GCAP_OSS_SHIFT 12
#define HDA_GCAP_STREAM_COUNT_MASK 0xf
#define HDA_GCAP_BSS_COUNT_MASK 0x1f
#define HDA_GCTL_CRST (1u << 0)
#define HDA_IRS_BUSY (1u << 0)
#define HDA_IRS_VALID (1u << 1)
#define HDA_CODEC_MASK 0x7fff
#define HDA_CODEC_COUNT 15
#define HDA_NODE_COUNT 128
#define HDA_COMMAND_CODEC_SHIFT 28
#define HDA_COMMAND_NODE_SHIFT 20
#define HDA_VERB_PAYLOAD_MASK 0xfffff
#define HDA_POINTER_RESET (1u << 15)
#define HDA_POINTER_MASK 0xff
#define HDA_RING_DMA (1u << 1)
#define HDA_CORB_ERROR (1u << 0)
#define HDA_RIRB_RESPONSE (1u << 0)
#define HDA_RIRB_RESPONSE_ENABLE (1u << 0)
#define HDA_RIRB_OVERRUN_ENABLE (1u << 2)
#define HDA_RIRB_OVERRUN (1u << 2)
#define HDA_RING_SIZE_MASK 3
#define HDA_RING_SELECT_2 0
#define HDA_RING_SELECT_16 1
#define HDA_RING_SELECT_256 2
#define HDA_RING_CAP_2 (1u << 4)
#define HDA_RING_CAP_16 (1u << 5)
#define HDA_RING_CAP_256 (1u << 6)
#define HDA_RESPONSE_CODEC_MASK 0xf
#define HDA_RESPONSE_UNSOLICITED (1u << 4)
#define HDA_SD_RESET (1u << 0)
#define HDA_SD_RUN (1u << 1)
#define HDA_SD_COMPLETION_ENABLE (1u << 2)
#define HDA_SD_FIFO_ERROR_ENABLE (1u << 3)
#define HDA_SD_DESCRIPTOR_ERROR_ENABLE (1u << 4)
#define HDA_SD_INTERRUPT_ENABLE (HDA_SD_COMPLETION_ENABLE | HDA_SD_FIFO_ERROR_ENABLE | \
    HDA_SD_DESCRIPTOR_ERROR_ENABLE)
#define HDA_GLOBAL_INTERRUPT_ENABLE (UINT32_C(1) << 31)
#define HDA_SD_TAG_SHIFT 20
#define HDA_SD_TAG_MASK (UINT32_C(0xf) << HDA_SD_TAG_SHIFT)
#define HDA_SD_CTL_MASK UINT32_C(0x00ffffff)
#define HDA_CAP_ID_SHIFT 16
#define HDA_CAP_ID_MASK 0xfff
#define HDA_CAP_NEXT_MASK 0xffff
#define HDA_CAP_LIMIT 16
#define HDA_PP_CAP_ID 0x3
#define HDA_ML_CAP_ID 0x2
#define HDA_PP_PPCTL 0x04
#define HDA_ML_LINK0 0x40
#define HDA_ML_LCAP 0x00
#define HDA_ML_LCTL 0x04
/* Intel's SOF driver allows 300 us for RUN to read back after it is set and
 * samples again after its 500 us poll interval; legacy Linux never reads it. */
#define HDA_RUN_LATCH_NS UINT64_C(1000000)
#define HDA_SD_COMPLETE (1u << 2)
#define HDA_SD_FIFO_ERROR (1u << 3)
#define HDA_SD_DESCRIPTOR_ERROR (1u << 4)
#define HDA_SD_STATUS_MASK (HDA_SD_COMPLETE | HDA_SD_FIFO_ERROR | HDA_SD_DESCRIPTOR_ERROR)
#define HDA_BDL_IOC (1u << 0)

#define HDA_OPERATION_TIMEOUT_MS 100
#define HDA_COMMAND_TIMEOUT_MS 100
#define HDA_POLL_MS 1

struct hda_response {
  uint32_t value, extended;
};

struct hda_buffer_descriptor {
  uint64_t address;
  uint32_t bytes, flags;
};

enum hda_boot_power { HDA_BOOT_POWER_ABSENT, HDA_BOOT_POWER_PRESENT,
                      HDA_BOOT_POWER_INVALID };

struct hda_boot_state {
  uint16_t command, pmcsr;
  unsigned power_capability;
  enum hda_boot_power power;
};

static uint8_t read8(const struct hda_controller *controller, unsigned offset)
{
  return *(volatile uint8_t *)(controller->registers.address + offset);
}

static uint16_t read16(const struct hda_controller *controller, unsigned offset)
{
  return *(volatile uint16_t *)(controller->registers.address + offset);
}

static uint32_t read32(const struct hda_controller *controller, unsigned offset)
{
  return *(volatile uint32_t *)(controller->registers.address + offset);
}

static void write8(struct hda_controller *controller, unsigned offset, uint8_t value)
{
  *(volatile uint8_t *)(controller->registers.address + offset) = value;
}

static void write16(struct hda_controller *controller, unsigned offset, uint16_t value)
{
  *(volatile uint16_t *)(controller->registers.address + offset) = value;
}

static void write32(struct hda_controller *controller, unsigned offset, uint32_t value)
{
  *(volatile uint32_t *)(controller->registers.address + offset) = value;
}

static bool wait8(struct hda_controller *controller, unsigned offset, uint8_t mask, uint8_t value)
{
  uint64_t deadline = task_deadline_after_ms(HDA_OPERATION_TIMEOUT_MS);
  do {
    if ((read8(controller, offset) & mask) == value) {
      return true;
    }
    kernel_task_sleep_until(task_deadline_after_ms(HDA_POLL_MS));
  } while (!task_deadline_expired(deadline));
  return false;
}

static bool wait16(struct hda_controller *controller, unsigned offset, uint16_t mask, uint16_t value)
{
  uint64_t deadline = task_deadline_after_ms(HDA_OPERATION_TIMEOUT_MS);
  do {
    if ((read16(controller, offset) & mask) == value) {
      return true;
    }
    kernel_task_sleep_until(task_deadline_after_ms(HDA_POLL_MS));
  } while (!task_deadline_expired(deadline));
  return false;
}

static bool wait32(struct hda_controller *controller, unsigned offset, uint32_t mask, uint32_t value)
{
  uint64_t deadline = task_deadline_after_ms(HDA_OPERATION_TIMEOUT_MS);
  do {
    if ((read32(controller, offset) & mask) == value) {
      return true;
    }
    kernel_task_sleep_until(task_deadline_after_ms(HDA_POLL_MS));
  } while (!task_deadline_expired(deadline));
  return false;
}

static bool set_bus_master(struct hda_controller *controller, bool enabled)
{
  uint64_t flags = cpu_save_interrupts();
  uint16_t command = pci_read16(controller->claim.device->address, PCI_COMMAND);
  command = (command & ~PCI_COMMAND_MASTER) | PCI_COMMAND_INTX_DISABLE;
  if (enabled) {
    command |= PCI_COMMAND_MASTER;
  }
  bool written = pci_write16(&controller->claim, PCI_COMMAND, command);
  uint16_t observed = pci_read16(controller->claim.device->address, PCI_COMMAND);
  cpu_restore_interrupts(flags);
  return written && !!(observed & PCI_COMMAND_MASTER) == enabled &&
      (observed & PCI_COMMAND_INTX_DISABLE);
}

static bool reset_stream(struct hda_controller *controller, uint8_t *status)
{
  unsigned offset = controller->stream + HDA_SD_CTL;
  write8(controller, offset, 0);
  if (!wait8(controller, offset, HDA_SD_RUN, 0)) {
    return false;
  }
  if (status) {
    *status = read8(controller, controller->stream + HDA_SD_STS);
  }
  write8(controller, controller->stream + HDA_SD_STS, HDA_SD_STATUS_MASK);
  write8(controller, offset, HDA_SD_RESET);
  if (!wait8(controller, offset, HDA_SD_RESET, HDA_SD_RESET)) {
    return false;
  }
  write8(controller, offset, 0);
  return wait8(controller, offset, HDA_SD_RESET, 0);
}

static bool mask_interrupts(struct hda_controller *controller)
{
  uint64_t flags = cpu_save_interrupts();
  write32(controller, HDA_INTCTL, 0);
  bool masked = read32(controller, HDA_INTCTL) == 0;
  bool msi_disabled = pci_msi_disable(&controller->msi);
  cpu_restore_interrupts(flags);
  return masked && msi_disabled;
}

/* The extended capability list lies in the mapped bootstrap window only when
 * every header does; a header outside it ends the walk. */
static unsigned find_capability(struct hda_controller *controller, unsigned id)
{
  unsigned offset = read16(controller, HDA_LLCH);
  for (unsigned i = 0; i < HDA_CAP_LIMIT && offset; ++i) {
    if (offset % 4 || offset > PCI_BOOTSTRAP_BAR_BYTES - 0x50) {
      return 0;
    }
    uint32_t header = read32(controller, offset);
    if (((header >> HDA_CAP_ID_SHIFT) & HDA_CAP_ID_MASK) == id) {
      return offset;
    }
    offset = header & HDA_CAP_NEXT_MASK;
  }
  return 0;
}

void hda_log_start_state(struct hda_controller *controller)
{
  audio_require_worker();
  if (!controller->link_ready || controller->failed || controller->shutdown) {
    return;
  }
  unsigned pp = find_capability(controller, HDA_PP_CAP_ID);
  unsigned ml = find_capability(controller, HDA_ML_CAP_ID);
  kwarn("hda: start state COMMAND=%x SDnCTL/STS=%x GCTL=%x DPLBASE=%x LLCH=%x PP=%x PPCTL=%x ML=%x LCAP=%x LCTL=%x\n",
      (unsigned)pci_read16(controller->claim.device->address, PCI_COMMAND),
      read32(controller, controller->stream + HDA_SD_CTL), read32(controller, HDA_GCTL),
      read32(controller, HDA_DPLBASE), (unsigned)read16(controller, HDA_LLCH), pp,
      pp ? read32(controller, pp + HDA_PP_PPCTL) : 0, ml,
      ml ? read32(controller, ml + HDA_ML_LINK0 + HDA_ML_LCAP) : 0,
      ml ? read32(controller, ml + HDA_ML_LINK0 + HDA_ML_LCTL) : 0);
}

void hda_shutdown(struct hda_controller *controller)
{
  audio_require_worker();
  if (!controller->prepared || controller->shutdown) {
    return;
  }
  controller->shutdown = true;
  bool interrupts_masked = mask_interrupts(controller);
  write16(controller, HDA_WAKEEN, 0);
  bool stream_reset = reset_stream(controller, NULL);
  write8(controller, HDA_CORBCTL, 0);
  write8(controller, HDA_RIRBCTL, 0);
  bool corb_stopped = wait8(controller, HDA_CORBCTL, HDA_RING_DMA, 0);
  bool rirb_stopped = wait8(controller, HDA_RIRBCTL, HDA_RING_DMA, 0);
  write32(controller, HDA_GCTL, 0);
  bool link_reset = wait32(controller, HDA_GCTL, HDA_GCTL_CRST, 0);
  bool dma_disabled = set_bus_master(controller, false);
  controller->command_ready = false;
  controller->stream_prepared = false;
  controller->stream_running = false;
  controller->link_ready = false;
  bool quiescent = interrupts_masked && stream_reset && corb_stopped && rirb_stopped &&
      link_reset && dma_disabled;
  if (!quiescent) {
    controller->failed = true;
  }
  if (controller->failed) {
    kerror("hda: shutdown IRQ-mask=%u stream-reset=%u CORB-stop=%u RIRB-stop=%u link-reset=%u BME-off=%u; backing %s until reboot\n",
        (unsigned)interrupts_masked, (unsigned)stream_reset, (unsigned)corb_stopped, (unsigned)rirb_stopped,
        (unsigned)link_reset, (unsigned)dma_disabled, quiescent ? "retained" : "quarantined");
  } else {
    kdebug("hda: shutdown IRQ-mask=%u stream-reset=%u CORB-stop=%u RIRB-stop=%u link-reset=%u BME-off=%u; backing retained until reboot\n",
        (unsigned)interrupts_masked, (unsigned)stream_reset, (unsigned)corb_stopped, (unsigned)rirb_stopped,
        (unsigned)link_reset, (unsigned)dma_disabled);
  }
}

void hda_fail(struct hda_controller *controller, const char *reason)
{
  audio_require_worker();
  if (controller->setup_attempt) {
    if (!controller->setup_failure) {
      controller->setup_failure = reason;
    }
    return;
  }
  if (!controller->failed) {
    kerror("hda: %s; engine unavailable until reboot\n", reason);
  }
  controller->failed = true;
  hda_shutdown(controller);
}

static bool fail_controller(struct hda_controller *controller, const char *reason)
{
  hda_fail(controller, reason);
  return false;
}

static bool configure_ring_size(struct hda_controller *controller, unsigned offset, unsigned *entries)
{
  uint8_t capabilities = read8(controller, offset);
  unsigned selection;
  if (capabilities & HDA_RING_CAP_256) {
    selection = HDA_RING_SELECT_256;
    *entries = 256;
  } else if (capabilities & HDA_RING_CAP_16) {
    selection = HDA_RING_SELECT_16;
    *entries = 16;
  } else if (capabilities & HDA_RING_CAP_2) {
    selection = HDA_RING_SELECT_2;
    *entries = 2;
  } else {
    return false;
  }
  write8(controller, offset, (capabilities & ~HDA_RING_SIZE_MASK) | selection);
  return (read8(controller, offset) & HDA_RING_SIZE_MASK) == selection;
}

/* Intel asks for misc backbone clock gating to be off while the link resets;
 * otherwise reset can miss command logic (Linux hda_intel_init_chip()). CGCTL
 * holds other controls too. Like Linux, only MISCBDCGE changes and the result
 * is advisory: the link and command checks decide whether the controller works. */
static void set_reset_clock_gating(struct hda_controller *controller, bool enabled)
{
  struct pci_address address = controller->claim.device->address;
  uint64_t flags = cpu_save_interrupts();
  uint32_t before = pci_read32(address, HDA_INTEL_CGCTL);
  uint32_t written = before & ~HDA_INTEL_CGCTL_MISCBDCGE;
  if (enabled) {
    written |= HDA_INTEL_CGCTL_MISCBDCGE;
  }
  if (written != before) {
    pci_write32(&controller->claim, HDA_INTEL_CGCTL, written);
  }
  uint32_t after = pci_read32(address, HDA_INTEL_CGCTL);
  cpu_restore_interrupts(flags);
  if (!!(after & HDA_INTEL_CGCTL_MISCBDCGE) != enabled) {
    kwarn("hda: Intel CGCTL MISCBDCGE %s not observed before=%x written=%x after=%x; continuing\n",
        enabled ? "set" : "clear", before, written, after);
  } else {
    kdebug("hda: Intel CGCTL before=%x written=%x after=%x\n", before, written, after);
  }
}

bool hda_link_start(struct hda_controller *controller)
{
  audio_require_worker();
  if (!controller->prepared || controller->failed || controller->shutdown) {
    return false;
  }
  if (controller->link_ready) {
    return true;
  }
  bool ungate = controller->profile->ungate_clocks_for_reset;
  if (ungate) {
    /* The link is already in reset; repeat entry so that all of reset happens
     * with clock gating off, as Linux's full controller reset does. */
    set_reset_clock_gating(controller, false);
    write32(controller, HDA_GCTL, 0);
    if (!wait32(controller, HDA_GCTL, HDA_GCTL_CRST, 0)) {
      set_reset_clock_gating(controller, true);
      return fail_controller(controller, "link reset entry timeout");
    }
  }
  /* RESET# needs >=100 us asserted; enumeration needs >=521 us after CRST=1. */
  kernel_task_sleep_until(task_deadline_after_ms(HDA_POLL_MS));
  write32(controller, HDA_GCTL, HDA_GCTL_CRST);
  if (!wait32(controller, HDA_GCTL, HDA_GCTL_CRST, HDA_GCTL_CRST)) {
    if (ungate) {
      set_reset_clock_gating(controller, true);
    }
    return fail_controller(controller, "link reset release timeout");
  }
  write32(controller, HDA_INTCTL, 0);
  write16(controller, HDA_WAKEEN, 0);
  write32(controller, HDA_DPLBASE, 0);
  write32(controller, HDA_DPUBASE, 0);
  kernel_task_sleep_until(task_deadline_after_ms(HDA_POLL_MS));
  /* Detection can already be latched while CRST is asserted. Read before W1C. */
  controller->codec_mask = read16(controller, HDA_STATESTS) & HDA_CODEC_MASK;
  write16(controller, HDA_STATESTS, controller->codec_mask);
  if (ungate) {
    set_reset_clock_gating(controller, true);
  }
  if (!controller->codec_mask) {
    return fail_controller(controller, "no codec reported after link reset");
  }
  controller->link_ready = true;
  kdebug("hda: link ready codec-mask=%x first-output-offset=%x\n",
      (unsigned)controller->codec_mask, controller->stream);
  return true;
}

bool hda_link_reset(struct hda_controller *controller)
{
  audio_require_worker();
  KASSERT(!controller->setup_attempt && !controller->stream_prepared &&
      !controller->stream_running);
  if (!controller->prepared || controller->failed || controller->shutdown) {
    return false;
  }
  bool interrupts_masked = mask_interrupts(controller);
  write16(controller, HDA_WAKEEN, 0);
  write8(controller, HDA_CORBCTL, 0);
  write8(controller, HDA_RIRBCTL, 0);
  bool corb_stopped = wait8(controller, HDA_CORBCTL, HDA_RING_DMA, 0);
  bool rirb_stopped = wait8(controller, HDA_RIRBCTL, HDA_RING_DMA, 0);
  write32(controller, HDA_GCTL, 0);
  bool link_reset = wait32(controller, HDA_GCTL, HDA_GCTL_CRST, 0);
  bool dma_disabled = set_bus_master(controller, false);
  controller->link_ready = false;
  controller->command_ready = false;
  controller->codec_mask = 0;
  if (!(interrupts_masked && corb_stopped && rirb_stopped && link_reset && dma_disabled)) {
    kinfo("hda: link reset IRQ-mask=%u CORB-stop=%u RIRB-stop=%u link-reset=%u BME-off=%u\n",
        (unsigned)interrupts_masked, (unsigned)corb_stopped, (unsigned)rirb_stopped,
        (unsigned)link_reset, (unsigned)dma_disabled);
    return false;
  }
  return true;
}

bool hda_commands_start(struct hda_controller *controller)
{
  audio_require_worker();
  if (!controller->link_ready || controller->failed || controller->shutdown) {
    return false;
  }
  if (controller->command_ready) {
    return true;
  }
  if (controller->profile->immediate_commands) {
    /* No command DMA; Immediate Command needs neither rings nor BME. */
    if (!wait16(controller, HDA_IRS, HDA_IRS_BUSY, 0)) {
      return fail_controller(controller, "immediate command interface busy at start");
    }
    controller->command_ready = true;
    kdebug("hda: immediate commands, one in flight; polling with delivery masked\n");
    return true;
  }
  write8(controller, HDA_CORBCTL, 0);
  write8(controller, HDA_RIRBCTL, 0);
  bool corb_stopped = wait8(controller, HDA_CORBCTL, HDA_RING_DMA, 0);
  bool rirb_stopped = wait8(controller, HDA_RIRBCTL, HDA_RING_DMA, 0);
  if (!corb_stopped || !rirb_stopped ||
      !configure_ring_size(controller, HDA_CORBSIZE, &controller->corb_entries) ||
      !configure_ring_size(controller, HDA_RIRBSIZE, &controller->rirb_entries)) {
    return fail_controller(controller, "command ring stop/size failed");
  }
  memset((void *)controller->corb.address, 0, controller->corb.bytes);
  memset((void *)controller->rirb.address, 0, controller->rirb.bytes);
  write32(controller, HDA_CORBLBASE, (uint32_t)controller->corb.physical);
  write32(controller, HDA_CORBUBASE, controller->corb.physical >> 32);
  write32(controller, HDA_RIRBLBASE, (uint32_t)controller->rirb.physical);
  write32(controller, HDA_RIRBUBASE, controller->rirb.physical >> 32);
  write16(controller, HDA_CORBRP, HDA_POINTER_RESET);
  if (!wait16(controller, HDA_CORBRP, HDA_POINTER_RESET, HDA_POINTER_RESET)) {
    return fail_controller(controller, "CORB pointer reset did not assert");
  }
  write16(controller, HDA_CORBRP, 0);
  write16(controller, HDA_CORBWP, 0);
  write16(controller, HDA_RIRBWP, HDA_POINTER_RESET);
  if (!wait16(controller, HDA_CORBRP, HDA_POINTER_RESET | HDA_POINTER_MASK, 0) ||
      !wait16(controller, HDA_RIRBWP, HDA_POINTER_RESET | HDA_POINTER_MASK, 0)) {
    return fail_controller(controller, "command ring pointer reset did not complete");
  }
  controller->corb_write = 0;
  controller->rirb_read = 0;
  write16(controller, HDA_RINTCNT, 1);
  write8(controller, HDA_CORBSTS, HDA_CORB_ERROR);
  write8(controller, HDA_RIRBSTS, HDA_RIRB_RESPONSE | HDA_RIRB_OVERRUN);
  dma_full_barrier();
  if (!set_bus_master(controller, true)) {
    return fail_controller(controller, "PCI bus mastering did not enable");
  }
  /* QEMU 10.2.2 resets its response count on RINTFL W1C. Enable status
   * generation while INTCTL=0 and PCI INTx delivery remain masked. */
  write8(controller, HDA_RIRBCTL,
      HDA_RING_DMA | HDA_RIRB_RESPONSE_ENABLE | HDA_RIRB_OVERRUN_ENABLE);
  write8(controller, HDA_CORBCTL, HDA_RING_DMA);
  if (!wait8(controller, HDA_RIRBCTL, HDA_RING_DMA, HDA_RING_DMA) ||
      !wait8(controller, HDA_CORBCTL, HDA_RING_DMA, HDA_RING_DMA)) {
    return fail_controller(controller, "command ring DMA did not start");
  }
  controller->command_ready = true;
  kdebug("hda: CORB=%u RIRB=%u, one command in flight; polling with delivery masked\n",
      controller->corb_entries, controller->rirb_entries);
  return true;
}

bool hda_commands_stop(struct hda_controller *controller)
{
  audio_require_worker();
  if (!controller->link_ready || controller->failed || controller->shutdown) {
    return false;
  }
  bool corb_stopped = true, rirb_stopped = true;
  if (controller->profile->immediate_commands) {
    corb_stopped = wait16(controller, HDA_IRS, HDA_IRS_BUSY, 0);
  } else {
    write8(controller, HDA_CORBCTL, 0);
    corb_stopped = wait8(controller, HDA_CORBCTL, HDA_RING_DMA, 0);
    write8(controller, HDA_RIRBCTL, 0);
    rirb_stopped = wait8(controller, HDA_RIRBCTL, HDA_RING_DMA, 0);
  }
  controller->command_ready = false;
  bool dma_disabled = controller->stream_running || set_bus_master(controller, false);
  if (!corb_stopped || !rirb_stopped || !dma_disabled) {
    return fail_controller(controller, "command stop or BME disable failed");
  }
  return true;
}

/* HDA 1.0a section 4.4.2: ICB starts one verb; IRV marks its response. The
 * response carries no codec address, so only one command is ever in flight. */
static bool immediate_command(struct hda_controller *controller, uint32_t command,
    uint32_t *response)
{
  uint16_t status = read16(controller, HDA_IRS);
  if (status & HDA_IRS_BUSY) {
    return fail_controller(controller, "immediate command busy before command");
  }
  write16(controller, HDA_IRS, HDA_IRS_VALID);
  write32(controller, HDA_IC, command);
  ++controller->commands;
  write16(controller, HDA_IRS, HDA_IRS_BUSY);
  uint64_t deadline = task_deadline_after_ms(HDA_COMMAND_TIMEOUT_MS);
  for (;;) {
    status = read16(controller, HDA_IRS);
    if ((status & (HDA_IRS_BUSY | HDA_IRS_VALID)) == HDA_IRS_VALID) {
      *response = read32(controller, HDA_IR);
      write16(controller, HDA_IRS, HDA_IRS_VALID);
      ++controller->responses;
      ktrace("hda: verb=%x response=%x immediate\n", command, *response);
      return true;
    }
    if (task_deadline_expired(deadline)) {
      kerror("hda: immediate command timeout verb=%x IRS=%x\n", command, (unsigned)status);
      return fail_controller(controller, "command deadline expired");
    }
    kernel_task_sleep_until(task_deadline_after_ms(HDA_POLL_MS));
  }
}

bool hda_command(struct hda_controller *controller, uint8_t codec, uint8_t node,
    uint32_t verb_payload, uint32_t *response)
{
  audio_require_worker();
  if (!controller->command_ready || controller->failed || controller->shutdown || !response ||
      codec >= HDA_CODEC_COUNT || node >= HDA_NODE_COUNT || verb_payload > HDA_VERB_PAYLOAD_MASK ||
      !(controller->codec_mask & (1u << codec))) {
    return false;
  }
  uint32_t command = ((uint32_t)codec << HDA_COMMAND_CODEC_SHIFT) |
      ((uint32_t)node << HDA_COMMAND_NODE_SHIFT) | verb_payload;
  if (controller->profile->immediate_commands) {
    return immediate_command(controller, command, response);
  }
  unsigned read_pointer = read16(controller, HDA_CORBRP) & HDA_POINTER_MASK;
  unsigned response_pointer = read16(controller, HDA_RIRBWP) & HDA_POINTER_MASK;
  if (read_pointer != controller->corb_write || response_pointer != controller->rirb_read) {
    return fail_controller(controller, "unexpected ring progress before command");
  }
  controller->corb_write = (controller->corb_write + 1) % controller->corb_entries;
  ((uint32_t *)controller->corb.address)[controller->corb_write] = command;
  dma_full_barrier();
  ++controller->commands;
  write16(controller, HDA_CORBWP, controller->corb_write);
  uint64_t deadline = task_deadline_after_ms(HDA_COMMAND_TIMEOUT_MS);
  for (;;) {
    uint8_t corb_status = read8(controller, HDA_CORBSTS);
    uint8_t rirb_status = read8(controller, HDA_RIRBSTS);
    if ((corb_status & HDA_CORB_ERROR) || (rirb_status & HDA_RIRB_OVERRUN)) {
      return fail_controller(controller, "CORB memory error or RIRB overrun");
    }
    unsigned write_pointer = read16(controller, HDA_RIRBWP) & HDA_POINTER_MASK;
    if (write_pointer >= controller->rirb_entries) {
      return fail_controller(controller, "RIRB write pointer outside configured ring");
    }
    bool received = false;
    uint32_t result = 0;
    while (controller->rirb_read != write_pointer) {
      controller->rirb_read = (controller->rirb_read + 1) % controller->rirb_entries;
      dma_read_barrier();
      struct hda_response entry =
          ((volatile struct hda_response *)controller->rirb.address)[controller->rirb_read];
      if (entry.extended & HDA_RESPONSE_UNSOLICITED) {
        ++controller->unsolicited;
        continue;
      }
      if (received || (entry.extended & HDA_RESPONSE_CODEC_MASK) != codec ||
          (entry.extended & ~(HDA_RESPONSE_CODEC_MASK | HDA_RESPONSE_UNSOLICITED))) {
        return fail_controller(controller, "solicited response does not match pending codec");
      }
      result = entry.value;
      ++controller->responses;
      received = true;
    }
    /* A response can arrive between the earlier status and pointer reads. */
    if (read8(controller, HDA_RIRBSTS) & HDA_RIRB_RESPONSE) {
      write8(controller, HDA_RIRBSTS, HDA_RIRB_RESPONSE);
    }
    if (received) {
      if ((read16(controller, HDA_CORBRP) & HDA_POINTER_MASK) != controller->corb_write) {
        return fail_controller(controller, "response arrived without CORB consumption");
      }
      *response = result;
      ktrace("hda: verb=%x response=%x CORB=%u RIRB=%u\n", command, result,
          (unsigned)controller->corb_write, (unsigned)controller->rirb_read);
      return true;
    }
    if (task_deadline_expired(deadline)) {
      kerror("hda: command timeout verb=%x CORBRP=%u CORBWP=%u RIRBWP=%u\n",
          command, (unsigned)(read16(controller, HDA_CORBRP) & HDA_POINTER_MASK),
          (unsigned)controller->corb_write, write_pointer);
      return fail_controller(controller, "command deadline expired");
    }
    kernel_task_sleep_until(task_deadline_after_ms(HDA_POLL_MS));
  }
}

bool hda_stream_prepare(struct hda_controller *controller)
{
  audio_require_worker();
  if (!controller->link_ready || controller->failed || controller->shutdown ||
      controller->stream_running) {
    return false;
  }
  controller->stream_prepared = false;
  if (!mask_interrupts(controller) || !reset_stream(controller, NULL)) {
    return fail_controller(controller, "output stream reset failed before start");
  }
  struct hda_buffer_descriptor *bdl = (void *)controller->bdl.address;
  for (unsigned i = 0; i < HDA_PERIOD_COUNT; ++i) {
    bdl[i] = (struct hda_buffer_descriptor){
      .address = controller->pcm.physical + i * HDA_PERIOD_BYTES,
      .bytes = HDA_PERIOD_BYTES, .flags = HDA_BDL_IOC,
    };
  }
  /* Like Linux's snd_hdac_stream_setup(), program the tag with a dword
   * read-modify-write; zero leaves SDnSTS's write-one-to-clear bits alone. */
  uint32_t control = read32(controller, controller->stream + HDA_SD_CTL) & HDA_SD_CTL_MASK;
  write32(controller, controller->stream + HDA_SD_CTL,
      (control & ~HDA_SD_TAG_MASK) | ((uint32_t)HDA_STREAM_TAG << HDA_SD_TAG_SHIFT));
  write32(controller, controller->stream + HDA_SD_CBL, HDA_BUFFER_BYTES);
  write16(controller, controller->stream + HDA_SD_LVI, HDA_PERIOD_COUNT - 1);
  write16(controller, controller->stream + HDA_SD_FMT, HDA_STREAM_FORMAT);
  write32(controller, controller->stream + HDA_SD_BDPL, (uint32_t)controller->bdl.physical);
  write32(controller, controller->stream + HDA_SD_BDPU, controller->bdl.physical >> 32);
  write8(controller, controller->stream + HDA_SD_STS, HDA_SD_STATUS_MASK);
  if (read32(controller, controller->stream + HDA_SD_CBL) != HDA_BUFFER_BYTES ||
      read16(controller, controller->stream + HDA_SD_LVI) != HDA_PERIOD_COUNT - 1 ||
      read16(controller, controller->stream + HDA_SD_FMT) != HDA_STREAM_FORMAT ||
      read32(controller, controller->stream + HDA_SD_BDPL) != (uint32_t)controller->bdl.physical ||
      read32(controller, controller->stream + HDA_SD_BDPU) != controller->bdl.physical >> 32) {
    return fail_controller(controller, "output descriptor configuration did not set");
  }
  if (controller->model == HDA_MODEL_NATIVE) {
    if (read8(controller, controller->stream + HDA_SD_CTL) & HDA_SD_RUN) {
      return fail_controller(controller, "native output running during FIFO inspection");
    }
    uint16_t encoded = read16(controller, controller->stream + HDA_SD_FIFOS);
    if (!encoded || encoded == UINT16_MAX) {
      return fail_controller(controller, "native output FIFO size unavailable");
    }
    /* Overestimate both byte-count and size-minus-one FIFO encodings. */
    uint32_t bytes = (uint32_t)encoded + 1;
    bytes = (bytes + HDA_FRAME_BYTES - 1) / HDA_FRAME_BYTES * HDA_FRAME_BYTES;
    if (bytes >= HDA_BUFFER_BYTES) {
      return fail_controller(controller, "native output FIFO exceeds PCM ring");
    }
    controller->fifo_bytes = bytes;
    kdebug("hda: native SDnFIFOS=%x FIFO-bytes=%u\n", (unsigned)encoded, bytes);
  }
  controller->stream_prepared = true;
  return true;
}

bool hda_stream_run_locked(struct hda_controller *controller)
{
  KASSERT(cpu_current() == cpu_bsp() && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  if (!controller->stream_prepared || !controller->link_ready || controller->stream_running ||
      controller->failed || controller->shutdown) {
    return false;
  }
  controller->stream_prepared = false;
  dma_full_barrier();
  if (!set_bus_master(controller, true)) {
    controller->start_failure = "PCI BME enable";
    return false;
  }
  controller->irq = (struct hda_irq_event){0};
  if (!pci_msi_enable(&controller->msi)) {
    controller->start_failure = "MSI enable";
    return false;
  }
  unsigned descriptor = (controller->stream - HDA_STREAM_BASE) / HDA_STREAM_BYTES;
  uint32_t interrupt_mask = HDA_GLOBAL_INTERRUPT_ENABLE | (1u << descriptor);
  write32(controller, HDA_INTCTL, interrupt_mask);
  controller->start_intctl = read32(controller, HDA_INTCTL);
  if (controller->start_intctl != interrupt_mask) {
    controller->start_failure = "INTCTL readback";
    return false;
  }
  controller->run_wallclock = read32(controller, HDA_WALCLK);
  controller->run_time = arch_monotonic_ns();
  write8(controller, controller->stream + HDA_SD_CTL, HDA_SD_RUN | HDA_SD_INTERRUPT_ENABLE);
  uint64_t deadline = controller->run_time + HDA_RUN_LATCH_NS;
  for (;;) {
    controller->start_ctl = read32(controller, controller->stream + HDA_SD_CTL);
    uint64_t now = arch_monotonic_ns();
    if ((controller->start_ctl & (HDA_SD_RUN | HDA_SD_INTERRUPT_ENABLE)) ==
        (HDA_SD_RUN | HDA_SD_INTERRUPT_ENABLE)) {
      controller->run_latch_ns = now - controller->run_time;
      break;
    }
    if (now >= deadline) {
      controller->start_failure = "SDnCTL RUN readback";
      return false;
    }
    __asm__ volatile("pause");
  }
  controller->stream_running = true;
  return true;
}

static bool collect_status(struct hda_controller *controller)
{
  uint8_t status = read8(controller, controller->stream + HDA_SD_STS) & HDA_SD_STATUS_MASK;
  if (!status) {
    return false;
  }
  if (!controller->irq.completed && !controller->irq.errors) {
    controller->irq.first_time = arch_monotonic_ns();
  }
  controller->irq.completed |= !!(status & HDA_SD_COMPLETE);
  controller->irq.errors |= status & (HDA_SD_FIFO_ERROR | HDA_SD_DESCRIPTOR_ERROR);
  /* Only the observed bits are acknowledged. Completion is a coalesced hint;
   * hardware position and time, never interrupt count, establish progress. */
  write8(controller, controller->stream + HDA_SD_STS, status);
  return true;
}

bool hda_interrupt(struct hda_controller *controller)
{
  KASSERT(cpu_current() == cpu_bsp() && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  if (!controller->prepared) {
    return false;
  }
  unsigned descriptor = (controller->stream - HDA_STREAM_BASE) / HDA_STREAM_BYTES;
  if (!(read32(controller, HDA_INTSTS) & (1u << descriptor))) {
    return false;
  }
  return collect_status(controller);
}

uint32_t hda_stream_wallclock_locked(struct hda_controller *controller)
{
  KASSERT(cpu_current() == cpu_bsp() && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(controller->stream_running);
  return read32(controller, HDA_WALCLK);
}

static bool position_in_ring(const struct hda_controller *controller, uint32_t bytes)
{
  if (controller->model == HDA_MODEL_NATIVE && bytes == HDA_BUFFER_BYTES) {
    bytes = 0;
  }
  /* LPIB counts bytes, not complete PCM frames (HDA 1.0a 3.3.37).
   * Only the qualified QEMU model promises frame-aligned observations. */
  return bytes < HDA_BUFFER_BYTES &&
      (controller->model == HDA_MODEL_NATIVE || bytes % HDA_FRAME_BYTES == 0);
}

enum hda_position_result hda_stream_position_locked(struct hda_controller *controller,
    struct hda_stream_position *position, struct hda_irq_event *event)
{
  KASSERT(cpu_current() == cpu_bsp() && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(position && event);
  *event = controller->irq;
  *position = (struct hda_stream_position){0};
  if (!controller->stream_running || controller->failed || controller->shutdown) {
    controller->start_failure = "stream unavailable for position observation";
    return HDA_POSITION_FAILED;
  }
  for (unsigned attempt = 0; attempt < 3; ++attempt) {
    uint32_t before = read32(controller, controller->stream + HDA_SD_LPIB);
    collect_status(controller);
    uint32_t wallclock = read32(controller, HDA_WALCLK);
    uint32_t bytes = read32(controller, controller->stream + HDA_SD_LPIB);
    controller->start_lpib[0] = before;
    controller->start_lpib[1] = bytes;
    if (!position_in_ring(controller, before) || !position_in_ring(controller, bytes)) {
      controller->start_failure = "position outside qualified PCM ring";
      return HDA_POSITION_FAILED;
    }
    if (before != bytes) {
      controller->start_failure = "LPIB unstable";
      continue;
    }
    *event = controller->irq;
    if (controller->model == HDA_MODEL_NATIVE && bytes == HDA_BUFFER_BYTES) {
      bytes = 0;
    }
    *position = (struct hda_stream_position){
      .bytes = bytes, .wallclock = wallclock, .completed = event->completed,
    };
    controller->start_ctl = read32(controller, controller->stream + HDA_SD_CTL);
    if (!(controller->start_ctl & HDA_SD_RUN)) {
      controller->start_failure = "RUN cleared by controller";
      return HDA_POSITION_FAILED;
    }
    return HDA_POSITION_STABLE;
  }
  /* Exhaustion alone does not establish a controller fault. Validate fresh
   * state and both last readings before asking the owner to discard the epoch. */
  collect_status(controller);
  controller->start_ctl = read32(controller, controller->stream + HDA_SD_CTL);
  controller->irq.errors |= (controller->start_ctl >> 24) &
      (HDA_SD_FIFO_ERROR | HDA_SD_DESCRIPTOR_ERROR);
  *event = controller->irq;
  if (!controller->link_ready || !controller->stream_running ||
      controller->failed || controller->shutdown ||
      !(read32(controller, HDA_GCTL) & HDA_GCTL_CRST) ||
      !(controller->start_ctl & HDA_SD_RUN) || event->errors) {
    controller->start_failure = "moving position with invalid stream state";
    return HDA_POSITION_FAILED;
  }
  controller->start_failure = "position kept moving through sample budget";
  return HDA_POSITION_MOVING;
}

bool hda_stream_abort(struct hda_controller *controller, uint8_t *errors)
{
  audio_require_worker();
  *errors = 0;
  if (!controller->link_ready || controller->failed || controller->shutdown) {
    return false;
  }
  bool interrupts_masked = mask_interrupts(controller);
  uint8_t status = 0;
  bool reset = reset_stream(controller, &status);
  uint64_t flags = cpu_save_interrupts();
  controller->stream_running = false;
  controller->stream_prepared = false;
  *errors = (status | controller->irq.errors) & (HDA_SD_FIFO_ERROR | HDA_SD_DESCRIPTOR_ERROR);
  controller->irq = (struct hda_irq_event){0};
  cpu_restore_interrupts(flags);
  bool dma_disabled = controller->command_ready || set_bus_master(controller, false);
  if (!interrupts_masked || !reset || !dma_disabled) {
    return fail_controller(controller, "output stop/reset or BME disable failed");
  }
  return true;
}

bool hda_stream_stop(struct hda_controller *controller)
{
  uint8_t errors;
  if (!hda_stream_abort(controller, &errors)) {
    return false;
  }
  if (errors) {
    return fail_controller(controller, "output FIFO/descriptor error at stop");
  }
  return true;
}

static bool boot_wait8(struct hda_controller *controller, unsigned offset, uint8_t mask)
{
  uint64_t deadline = task_deadline_after_ms(HDA_OPERATION_TIMEOUT_MS);
  while (read8(controller, offset) & mask) {
    arch_clock_maintain();
    if (task_deadline_expired(deadline)) {
      return false;
    }
    __asm__ volatile("pause");
  }
  return true;
}

static bool boot_halt(struct hda_controller *controller)
{
  uint16_t capabilities = read16(controller, HDA_GCAP);
  unsigned input = (capabilities >> HDA_GCAP_ISS_SHIFT) & HDA_GCAP_STREAM_COUNT_MASK;
  unsigned output = (capabilities >> HDA_GCAP_OSS_SHIFT) & HDA_GCAP_STREAM_COUNT_MASK;
  unsigned bidirectional = (capabilities >> HDA_GCAP_BSS_SHIFT) & HDA_GCAP_BSS_COUNT_MASK;
  unsigned streams = input + output + bidirectional;
  if (read8(controller, HDA_VMAJ) != HDA_VERSION_MAJOR || !output ||
      HDA_STREAM_BASE + streams * HDA_STREAM_BYTES > PCI_BOOTSTRAP_BAR_BYTES) {
    return false;
  }
  write32(controller, HDA_INTCTL, 0);
  write16(controller, HDA_WAKEEN, 0);
  for (unsigned i = 0; i < streams; ++i) {
    unsigned offset = HDA_STREAM_BASE + i * HDA_STREAM_BYTES + HDA_SD_CTL;
    write8(controller, offset, 0);
    if (!boot_wait8(controller, offset, HDA_SD_RUN)) {
      return false;
    }
  }
  write8(controller, HDA_CORBCTL, 0);
  write8(controller, HDA_RIRBCTL, 0);
  bool corb_stopped = boot_wait8(controller, HDA_CORBCTL, HDA_RING_DMA);
  bool rirb_stopped = boot_wait8(controller, HDA_RIRBCTL, HDA_RING_DMA);
  if (!corb_stopped || !rirb_stopped) {
    return false;
  }
  write32(controller, HDA_GCTL, 0);
  uint64_t deadline = task_deadline_after_ms(HDA_OPERATION_TIMEOUT_MS);
  while (read32(controller, HDA_GCTL) & HDA_GCTL_CRST) {
    arch_clock_maintain();
    if (task_deadline_expired(deadline)) {
      return false;
    }
    __asm__ volatile("pause");
  }
  return true;
}

/* Explicit per-controller quirks. Linux references are to sound/hda at
 * torvalds/linux af32da41b0327b9c6a37856ba82b6760d6c8d10e; see hda.md. */
static const struct hda_controller_profile controller_profiles[] = {
  {
    .name = "QEMU ICH6", .vendor = HDA_INTEL_VENDOR, .device = HDA_QEMU_DEVICE,
    .subclass = HDA_PCI_SUBCLASS, .model = HDA_MODEL_QEMU,
    .coherence = HDA_COHERENCE_NONE, .dma_maximum = UINT64_MAX,
  },
  {
    /* Linux: AZX_DCAPS_INTEL_LNL (PCH snoop, TCSEL, CGCTL around reset and
     * PIO commands) for PCI_DEVICE_ID_INTEL_HDA_PTL_H. */
    .name = "Intel Panther Lake-H", .vendor = HDA_INTEL_VENDOR,
    .device = HDA_INTEL_PTL_H_DEVICE, .subclass = HDA_PCI_AUDIO_SUBCLASS,
    .model = HDA_MODEL_NATIVE, .coherence = HDA_COHERENCE_INTEL_DEVC,
    .dma_maximum = UINT64_MAX, .clear_tcsel = true, .ungate_clocks_for_reset = true,
    .immediate_commands = true,
  },
  {
    /* Linux limits every AMD HDA controller to 40-bit DMA. */
    .name = "AMD", .vendor = HDA_AMD_VENDOR, .any_device = true,
    .subclass = HDA_PCI_SUBCLASS, .model = HDA_MODEL_NATIVE,
    .coherence = HDA_COHERENCE_PCIE, .dma_maximum = HDA_DMA_MAX_40,
  },
};

static const struct hda_controller_profile unlisted_profile = {
  .name = "unlisted", .subclass = HDA_PCI_SUBCLASS, .model = HDA_MODEL_NATIVE,
  .coherence = HDA_COHERENCE_PCIE, .dma_maximum = UINT64_MAX,
};

static const struct hda_controller_profile *controller_profile(const struct pci_device *device)
{
  for (size_t i = 0; i < sizeof(controller_profiles) / sizeof(controller_profiles[0]); ++i) {
    const struct hda_controller_profile *profile = &controller_profiles[i];
    if (device->vendor_id == profile->vendor &&
        (profile->any_device || device->device_id == profile->device)) {
      return profile;
    }
  }
  return &unlisted_profile;
}

/* A GPU's display audio sits beside its display function 0 and shares its
 * vendor. AMD APUs also place the analog controller in the GPU's device, but
 * under the CPU vendor, so the vendor comparison keeps it. */
static bool display_audio(const struct pci_device *device)
{
  if (!device->address.function) {
    return false;
  }
  for (size_t i = 0; i < pci_device_count(); ++i) {
    const struct pci_device *sibling = pci_device_at(i);
    if (sibling->address.bus == device->address.bus &&
        sibling->address.device == device->address.device &&
        sibling->address.function == 0) {
      return sibling->base_class == HDA_PCI_DISPLAY_CLASS &&
          sibling->vendor_id == device->vendor_id;
    }
  }
  return false;
}

static enum pci_selection select_controller(size_t *selected,
    const struct hda_controller_profile **selected_profile)
{
  if (pci_inventory_state() != PCI_INVENTORY_COMPLETE) {
    return PCI_SELECTION_INCOMPLETE;
  }
  bool found = false;
  for (size_t i = 0; i < pci_device_count(); ++i) {
    const struct pci_device *device = pci_device_at(i);
    const struct hda_controller_profile *profile = controller_profile(device);
    if (device->base_class != HDA_PCI_CLASS || device->subclass != profile->subclass ||
        device->interface != HDA_PCI_INTERFACE) {
      continue;
    }
    if (display_audio(device)) {
      kdebug("hda: PCI=%u:%u.%u %x:%x is display audio; skipped\n",
          (unsigned)device->address.bus, (unsigned)device->address.device,
          (unsigned)device->address.function, (unsigned)device->vendor_id,
          (unsigned)device->device_id);
      continue;
    }
    if (found) {
      return PCI_SELECTION_AMBIGUOUS;
    }
    found = true;
    *selected = i;
    *selected_profile = profile;
  }
  return found ? PCI_SELECTION_UNIQUE : PCI_SELECTION_ABSENT;
}

static const char *prepare_pcie_coherence(struct hda_controller *controller)
{
  struct pci_claim *claim = &controller->claim;
  struct pci_address address = claim->device->address;
  if (pci_read16(address, PCI_COMMAND) & PCI_COMMAND_MASTER) {
    return "native PCI BME enabled before coherence setup";
  }
  unsigned capability = 0;
  for (unsigned i = 0; i < claim->capability_count; ++i) {
    unsigned offset = claim->capabilities[i];
    if (pci_read8(address, offset + PCI_CAP_ID) != PCI_CAP_EXPRESS) {
      continue;
    }
    if (capability) {
      return "native PCIe capability is ambiguous";
    }
    if (!pci_capability_fits(claim, offset, HDA_PCIE_DEVICE_CONTROL_BYTES)) {
      return "native PCIe capability does not contain Device Control";
    }
    capability = offset;
  }
  if (!capability) {
    return "native PCIe capability is missing";
  }
  uint16_t flags = pci_read16(address, capability + HDA_PCIE_FLAGS);
  unsigned version = flags & HDA_PCIE_VERSION_MASK;
  unsigned type = (flags >> HDA_PCIE_TYPE_SHIFT) & HDA_PCIE_TYPE_MASK;
  uint16_t control = pci_read16(address, capability + HDA_PCIE_DEVICE_CONTROL);
  kdebug("hda: native PCIe cap=%x flags=%x DEVCTL-before=%x\n",
      capability, (unsigned)flags, (unsigned)control);
  if ((version != 1 && version != 2) ||
      (type != HDA_PCIE_ENDPOINT && type != HDA_PCIE_ROOT_ENDPOINT) ||
      (flags & (HDA_PCIE_SLOT_IMPLEMENTED | HDA_PCIE_RESERVED_FLAGS)) ||
      control == UINT16_MAX || (control & HDA_PCIE_FUNCTION_RESET)) {
    return "native PCIe Device Control state is invalid";
  }
  /* BME stays off and FLR stays clear. Use a word write because the adjacent
   * Device Status word has W1C bits. */
  uint16_t expected = control & ~HDA_PCIE_NO_SNOOP;
  if (pci_read16(address, PCI_COMMAND) & PCI_COMMAND_MASTER) {
    return "native PCI BME enabled during coherence setup";
  }
  if (expected != control) {
    pci_write16(claim, capability + HDA_PCIE_DEVICE_CONTROL, expected);
  }
  uint16_t observed = pci_read16(address, capability + HDA_PCIE_DEVICE_CONTROL);
  kdebug("hda: native PCIe DEVCTL-requested=%x after=%x NoSnoop=%u\n",
      (unsigned)expected, (unsigned)observed, (unsigned)!!(observed & HDA_PCIE_NO_SNOOP));
  if (observed != expected || (pci_read16(address, PCI_COMMAND) & PCI_COMMAND_MASTER)) {
    return "native PCIe NoSnoop clear did not verify with BME off";
  }
  return NULL;
}

/* Intel PCH controllers without a PCIe capability carry No Snoop in their
 * vendor DEVC register (Linux azx_init_pci(), AZX_SNOOP_TYPE_SCH). TCSEL
 * selects the PCIe traffic class; Linux clears it to avoid playback static. */
static const char *prepare_intel_coherence(struct hda_controller *controller)
{
  struct pci_claim *claim = &controller->claim;
  struct pci_address address = claim->device->address;
  if (pci_read16(address, PCI_COMMAND) & PCI_COMMAND_MASTER) {
    return "Intel PCI BME enabled before coherence setup";
  }
  if (controller->profile->clear_tcsel) {
    uint8_t tcsel = pci_read8(address, HDA_INTEL_TCSEL);
    if (tcsel & HDA_INTEL_TCSEL_MASK) {
      pci_write8(claim, HDA_INTEL_TCSEL, tcsel & ~HDA_INTEL_TCSEL_MASK);
    }
    /* Linux clears TCSEL against playback static without checking it. */
    uint8_t observed = pci_read8(address, HDA_INTEL_TCSEL);
    if (observed & HDA_INTEL_TCSEL_MASK) {
      kwarn("hda: Intel TCSEL clear not observed before=%x after=%x; continuing\n",
          (unsigned)tcsel, (unsigned)observed);
    } else {
      kdebug("hda: Intel TCSEL before=%x after=%x\n", (unsigned)tcsel, (unsigned)observed);
    }
  }
  uint16_t control = pci_read16(address, HDA_INTEL_DEVC);
  if (control == UINT16_MAX) {
    return "Intel DEVC unreadable";
  }
  uint16_t expected = control & ~HDA_INTEL_DEVC_NO_SNOOP;
  if (expected != control) {
    pci_write16(claim, HDA_INTEL_DEVC, expected);
  }
  uint16_t observed = pci_read16(address, HDA_INTEL_DEVC);
  kdebug("hda: Intel DEVC before=%x after=%x NoSnoop=%u\n", (unsigned)control,
      (unsigned)observed, (unsigned)!!(observed & HDA_INTEL_DEVC_NO_SNOOP));
  if (observed != expected || (pci_read16(address, PCI_COMMAND) & PCI_COMMAND_MASTER)) {
    return "Intel DEVC No Snoop clear did not verify with BME off";
  }
  return NULL;
}

static struct hda_boot_state capture_boot_state(const struct pci_device *device)
{
  struct pci_address address = device->address;
  struct hda_boot_state state = {.command = pci_read16(address, PCI_COMMAND)};
  struct pci_claim layout = {0};
  bool seen[PCI_CONVENTIONAL_BYTES / PCI_REGISTER_BYTES] = {0};
  unsigned offset = 0;
  if (pci_read16(address, PCI_STATUS) & PCI_STATUS_CAPABILITIES) {
    offset = pci_read8(address, PCI_CAPABILITIES) & PCI_CAP_POINTER_MASK;
  }
  while (offset) {
    if (offset < PCI_CAP_FIRST || seen[offset / PCI_REGISTER_BYTES] ||
        layout.capability_count == PCI_CAP_COUNT) {
      state.power = HDA_BOOT_POWER_INVALID;
      break;
    }
    seen[offset / PCI_REGISTER_BYTES] = true;
    layout.capabilities[layout.capability_count++] = offset;
    offset = pci_read8(address, offset + PCI_CAP_NEXT) & PCI_CAP_POINTER_MASK;
  }
  if (state.power != HDA_BOOT_POWER_INVALID) {
    for (unsigned i = 0; i < layout.capability_count; ++i) {
      offset = layout.capabilities[i];
      if (pci_read8(address, offset + PCI_CAP_ID) != PCI_CAP_POWER) {
        continue;
      }
      if (state.power_capability || !pci_capability_fits(&layout, offset, PCI_POWER_BYTES)) {
        state.power = HDA_BOOT_POWER_INVALID;
        break;
      }
      state.power_capability = offset;
      state.power = HDA_BOOT_POWER_PRESENT;
    }
  }
  /* Reservation can reject firmware MSI before the PCI probe fills its snapshot.
   * This read-only capture records boot state without enabling configuration writes. */
  kdebug("hda: boot PCI=%u:%u.%u COMMAND=%x Mem=%u BME=%u\n",
      (unsigned)address.bus, (unsigned)address.device, (unsigned)address.function,
      (unsigned)state.command, !!(state.command & PCI_COMMAND_MEMORY),
      !!(state.command & PCI_COMMAND_MASTER));
  if (state.power == HDA_BOOT_POWER_PRESENT) {
    state.pmcsr = pci_read16(address, state.power_capability + PCI_POWER_CONTROL);
    kdebug("hda: boot PM-cap=%x PMCSR=%x D%u NoSoftRst=%u PME-enable=%u PME-status=%u\n",
        state.power_capability, (unsigned)state.pmcsr,
        (unsigned)(state.pmcsr & PCI_POWER_STATE_MASK),
        !!(state.pmcsr & PCI_POWER_NO_SOFT_RESET), !!(state.pmcsr & PCI_POWER_PME_ENABLE),
        !!(state.pmcsr & PCI_POWER_PME_STATUS));
  } else {
    kdebug("hda: boot PM capability %s; power state unavailable\n",
        state.power == HDA_BOOT_POWER_INVALID ? "invalid" : "absent");
  }
  return state;
}

static const char *native_probe_failure(const struct hda_boot_state *state)
{
  if (state->power == HDA_BOOT_POWER_INVALID) {
    return "native PCI PM capability unusable";
  }
  if (state->power == HDA_BOOT_POWER_PRESENT &&
      (state->pmcsr & PCI_POWER_STATE_MASK) != PCI_POWER_D0 &&
      (state->command & PCI_COMMAND_MASTER)) {
    return "native PCI non-D0 boot state with BME on; wake refused";
  }
  if (!(state->command & PCI_COMMAND_MEMORY) && (state->command & PCI_COMMAND_MASTER)) {
    return "native PCI memory decode disabled with BME on; probe refused";
  }
  if (state->power == HDA_BOOT_POWER_PRESENT &&
      (state->pmcsr & PCI_POWER_STATE_MASK) == PCI_POWER_D3HOT &&
      !(state->pmcsr & PCI_POWER_NO_SOFT_RESET)) {
    return "native PCI D3hot wake would reset BARs; probe refused";
  }
  return "native PCI D0/memory-decode setup failed";
}

void hda_prepare(struct hda_controller *controller, const struct boot_info *boot)
{
  KASSERT(cpu_current() == cpu_bsp() && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(!controller->claim.device && !controller->prepared);
  size_t selected = 0;
  enum pci_selection selection = select_controller(&selected, &controller->profile);
  if (selection != PCI_SELECTION_UNIQUE) {
    if (selection == PCI_SELECTION_ABSENT) {
      kdebug("hda: controller absent; inactive\n");
    } else {
      kinfo("hda: controller selection=%u; inactive\n", (unsigned)selection);
    }
    return;
  }
  controller->model = controller->profile->model;
  const struct pci_device *candidate = pci_device_at(selected);
  kdebug("hda: PCI=%u:%u.%u %x:%x profile=%s\n", (unsigned)candidate->address.bus,
      (unsigned)candidate->address.device, (unsigned)candidate->address.function,
      (unsigned)candidate->vendor_id, (unsigned)candidate->device_id, controller->profile->name);
  struct hda_boot_state boot_state = capture_boot_state(candidate);
  if (!pci_reserve_device_at(selected, &controller->claim)) {
    kerror("hda: cannot reserve controller; inactive\n");
    return;
  }
  struct pci_device *device = controller->claim.device;
  bool probe_ready = pci_begin_mmio_probe(&controller->claim, &controller->firmware);
  const char *probe_failure = "BAR0 bootstrap unavailable";
  if (!probe_ready && controller->model == HDA_MODEL_NATIVE) {
    probe_failure = native_probe_failure(&boot_state);
  }
  if (!probe_ready ||
      pci_map_bootstrap_bar(&controller->claim, 0, 0, PCI_BOOTSTRAP_BAR_BYTES, boot,
                            &controller->registers) != MM_OK) {
    if (pci_restore_mmio_probe(&controller->claim, &controller->firmware)) {
      pci_cancel_reservation(&controller->claim);
      kwarn("hda: %s; inactive\n", probe_failure);
    } else {
      controller->failed = true;
      kerror("hda: %s; probe restore failed; reservation retained until reboot\n", probe_failure);
    }
    return;
  }
  /* Halt firmware engines before claim completion changes BME/INTx or sizes BARs.
   * No driver DMA is published here; link release belongs to the worker. */
  if (!boot_halt(controller) || !pci_complete_claim(&controller->claim)) {
    controller->failed = true;
    kerror("hda: boot halt/claim failed; ownership retained until reboot\n");
    return;
  }
  uint16_t command = pci_read16(device->address, PCI_COMMAND);
  pci_write16(&controller->claim, PCI_COMMAND, command & ~(PCI_COMMAND_IO | PCI_COMMAND_MEMORY));
  if (pci_read16(device->address, PCI_COMMAND) &
      (PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER)) {
    controller->failed = true;
    kerror("hda: BAR decoding disable failed; ownership retained until reboot\n");
    return;
  }
  bool sized = pci_size_bars(&controller->claim);
  pci_write16(&controller->claim, PCI_COMMAND, (command & ~PCI_COMMAND_IO) | PCI_COMMAND_MEMORY);
  uint16_t observed = pci_read16(device->address, PCI_COMMAND);
  uint16_t command_mask = PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER |
      PCI_COMMAND_INTX_DISABLE;
  if (!sized || controller->claim.bars[0].bytes < PCI_BOOTSTRAP_BAR_BYTES ||
      (observed & command_mask) != (PCI_COMMAND_MEMORY | PCI_COMMAND_INTX_DISABLE)) {
    controller->failed = true;
    kerror("hda: BAR0 extent rejected; ownership retained until reboot\n");
    return;
  }
  uint16_t capabilities = read16(controller, HDA_GCAP);
  unsigned input = (capabilities >> HDA_GCAP_ISS_SHIFT) & HDA_GCAP_STREAM_COUNT_MASK;
  unsigned output = (capabilities >> HDA_GCAP_OSS_SHIFT) & HDA_GCAP_STREAM_COUNT_MASK;
  controller->stream = HDA_STREAM_BASE + input * HDA_STREAM_BYTES;
  kdebug("hda: PCI=%u:%u.%u GCAP=%x ISS=%u OSS=%u version=%u.%u\n",
      (unsigned)device->address.bus, (unsigned)device->address.device,
      (unsigned)device->address.function, (unsigned)capabilities, input, output,
      (unsigned)read8(controller, HDA_VMAJ), (unsigned)read8(controller, HDA_VMIN));
  const char *failure = NULL;
  if (controller->profile->coherence == HDA_COHERENCE_PCIE) {
    failure = prepare_pcie_coherence(controller);
  } else if (controller->profile->coherence == HDA_COHERENCE_INTEL_DEVC) {
    failure = prepare_intel_coherence(controller);
  }
  if (failure) {
    controller->failed = true;
    kerror("hda: %s; ownership retained until reboot\n", failure);
    return;
  }
  /* Allocate within the controller's DMA reach rather than refusing later. */
  uint64_t maximum = HDA_DMA_MAX_32;
  if (capabilities & HDA_GCAP_64OK) {
    maximum = controller->profile->dma_maximum;
  }
  /* Immediate-command controllers never run the command rings. */
  bool rings = !controller->profile->immediate_commands;
  if (!output || controller->stream + HDA_STREAM_BYTES > PCI_BOOTSTRAP_BAR_BYTES ||
      (rings && dma_buffer_allocate_below(&controller->corb, PAGE_SIZE, maximum) != MM_OK) ||
      (rings && dma_buffer_allocate_below(&controller->rirb, PAGE_SIZE, maximum) != MM_OK) ||
      dma_buffer_allocate_below(&controller->bdl, PAGE_SIZE, maximum) != MM_OK ||
      dma_buffer_allocate_below(&controller->pcm, HDA_BUFFER_BYTES, maximum) != MM_OK) {
    controller->failed = true;
    kerror("hda: output or coherent DMA within %lx unavailable; backing retained until reboot\n",
        maximum);
    return;
  }
  if (!pci_msi_prepare(&controller->claim, &controller->msi, APIC_HDA_VECTOR)) {
    controller->failed = true;
    kerror("hda: single-message MSI unavailable; ownership retained until reboot\n");
    return;
  }
  controller->prepared = true;
  kdebug("hda: prepared output descriptor=%u PCM=%u backing=%zu DMA-max=%lx commands=%s; link reset, BME off\n",
      input, HDA_BUFFER_BYTES, controller->pcm.bytes, maximum, rings ? "rings" : "immediate");
}
