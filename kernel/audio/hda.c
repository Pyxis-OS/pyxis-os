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

#define HDA_QEMU_VENDOR 0x8086
#define HDA_QEMU_DEVICE 0x2668
#define HDA_AMD_VENDOR 0x1022
#define HDA_AMD_DEVICE 0x15e3
#define HDA_LENOVO_VENDOR 0x17aa
#define HDA_LENOVO_DEVICE 0x5081
#define HDA_PCI_SUBSYSTEM_VENDOR 0x2c
#define HDA_PCI_SUBSYSTEM_DEVICE 0x2e
#define HDA_PCI_CLASS 0x04
#define HDA_PCI_SUBCLASS 0x03
#define HDA_PCI_INTERFACE 0x00
#define HDA_AMD_SNOOP_CONTROL 0x42
#define HDA_AMD_SNOOP_MASK 0x07
#define HDA_AMD_SNOOP_ENABLE 0x02
#define HDA_AMD_DMA_MAX ((UINT64_C(1) << 40) - 1)
#define HDA_VERSION_MAJOR 1
#define HDA_DMA_ALIGNMENT 128

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
  pci_write16(&controller->claim, PCI_COMMAND, command);
  uint16_t observed = pci_read16(controller->claim.device->address, PCI_COMMAND);
  cpu_restore_interrupts(flags);
  return !!(observed & PCI_COMMAND_MASTER) == enabled &&
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
    klog("hda: shutdown IRQ-mask=%u stream-reset=%u CORB-stop=%u RIRB-stop=%u link-reset=%u BME-off=%u; backing %s until reboot\n",
        (unsigned)interrupts_masked, (unsigned)stream_reset, (unsigned)corb_stopped, (unsigned)rirb_stopped,
        (unsigned)link_reset, (unsigned)dma_disabled, quiescent ? "retained" : "quarantined");
  } else {
    ktrace("hda: shutdown IRQ-mask=%u stream-reset=%u CORB-stop=%u RIRB-stop=%u link-reset=%u BME-off=%u; backing retained until reboot\n",
        (unsigned)interrupts_masked, (unsigned)stream_reset, (unsigned)corb_stopped, (unsigned)rirb_stopped,
        (unsigned)link_reset, (unsigned)dma_disabled);
  }
}

void hda_fail(struct hda_controller *controller, const char *reason)
{
  audio_require_worker();
  if (!controller->failed) {
    klog("hda: %s; engine unavailable until reboot\n", reason);
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

bool hda_link_start(struct hda_controller *controller)
{
  audio_require_worker();
  if (!controller->prepared || controller->failed || controller->shutdown) {
    return false;
  }
  if (controller->link_ready) {
    return true;
  }
  /* RESET# needs >=100 us asserted; enumeration needs >=521 us after CRST=1. */
  kernel_task_sleep_until(task_deadline_after_ms(HDA_POLL_MS));
  write32(controller, HDA_GCTL, HDA_GCTL_CRST);
  if (!wait32(controller, HDA_GCTL, HDA_GCTL_CRST, HDA_GCTL_CRST)) {
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
  if (!controller->codec_mask) {
    return fail_controller(controller, "no codec reported after link reset");
  }
  controller->link_ready = true;
  ktrace("hda: link ready codec-mask=%x first-output-offset=%x\n",
      (unsigned)controller->codec_mask, controller->stream);
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
  ktrace("hda: CORB=%u RIRB=%u, one command in flight; polling with delivery masked\n",
      controller->corb_entries, controller->rirb_entries);
  return true;
}

bool hda_commands_stop(struct hda_controller *controller)
{
  audio_require_worker();
  if (!controller->link_ready || controller->failed || controller->shutdown) {
    return false;
  }
  write8(controller, HDA_CORBCTL, 0);
  bool corb_stopped = wait8(controller, HDA_CORBCTL, HDA_RING_DMA, 0);
  write8(controller, HDA_RIRBCTL, 0);
  bool rirb_stopped = wait8(controller, HDA_RIRBCTL, HDA_RING_DMA, 0);
  controller->command_ready = false;
  bool dma_disabled = controller->stream_running || set_bus_master(controller, false);
  if (!corb_stopped || !rirb_stopped || !dma_disabled) {
    return fail_controller(controller, "command ring stop or BME disable failed");
  }
  return true;
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
  unsigned read_pointer = read16(controller, HDA_CORBRP) & HDA_POINTER_MASK;
  unsigned response_pointer = read16(controller, HDA_RIRBWP) & HDA_POINTER_MASK;
  if (read_pointer != controller->corb_write || response_pointer != controller->rirb_read) {
    return fail_controller(controller, "unexpected ring progress before command");
  }
  uint32_t command = ((uint32_t)codec << HDA_COMMAND_CODEC_SHIFT) |
      ((uint32_t)node << HDA_COMMAND_NODE_SHIFT) | verb_payload;
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
      klog("hda: command timeout verb=%x CORBRP=%u CORBWP=%u RIRBWP=%u\n",
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
  write8(controller, controller->stream + HDA_SD_CTL + 2,
      HDA_STREAM_TAG << (HDA_SD_TAG_SHIFT - 16));
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
  if (controller->model == HDA_MODEL_AMD) {
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
    ktrace("hda: native SDnFIFOS=%x FIFO-bytes=%u\n", (unsigned)encoded, bytes);
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
    return false;
  }
  controller->irq = (struct hda_irq_event){0};
  bool interrupt_enabled = pci_msi_enable(&controller->msi);
  if (interrupt_enabled) {
    unsigned descriptor = (controller->stream - HDA_STREAM_BASE) / HDA_STREAM_BYTES;
    uint32_t interrupt_mask = HDA_GLOBAL_INTERRUPT_ENABLE | (1u << descriptor);
    write32(controller, HDA_INTCTL, interrupt_mask);
    interrupt_enabled = read32(controller, HDA_INTCTL) == interrupt_mask;
    if (interrupt_enabled) {
      controller->run_wallclock = read32(controller, HDA_WALCLK);
      controller->run_time = arch_monotonic_ns();
      write8(controller, controller->stream + HDA_SD_CTL, HDA_SD_RUN | HDA_SD_INTERRUPT_ENABLE);
      interrupt_enabled = (read8(controller, controller->stream + HDA_SD_CTL) &
          (HDA_SD_RUN | HDA_SD_INTERRUPT_ENABLE)) == (HDA_SD_RUN | HDA_SD_INTERRUPT_ENABLE);
      controller->stream_running = interrupt_enabled;
    }
  }
  return interrupt_enabled;
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

bool hda_stream_position_locked(struct hda_controller *controller,
    struct hda_stream_position *position, struct hda_irq_event *event)
{
  KASSERT(cpu_current() == cpu_bsp() && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(position && event);
  *event = controller->irq;
  *position = (struct hda_stream_position){0};
  if (!controller->stream_running || controller->failed || controller->shutdown) {
    return false;
  }
  for (unsigned attempt = 0; attempt < 3; ++attempt) {
    uint32_t before = read32(controller, controller->stream + HDA_SD_LPIB);
    collect_status(controller);
    uint32_t wallclock = read32(controller, HDA_WALCLK);
    uint32_t bytes = read32(controller, controller->stream + HDA_SD_LPIB);
    if (before != bytes) {
      continue;
    }
    *event = controller->irq;
    if (controller->model == HDA_MODEL_AMD && bytes == HDA_BUFFER_BYTES) {
      bytes = 0;
    }
    *position = (struct hda_stream_position){
      .bytes = bytes, .wallclock = wallclock, .completed = event->completed,
    };
    return bytes < HDA_BUFFER_BYTES &&
        (read8(controller, controller->stream + HDA_SD_CTL) & HDA_SD_RUN);
  }
  return false;
}

bool hda_stream_stop(struct hda_controller *controller)
{
  audio_require_worker();
  if (!controller->link_ready || controller->failed || controller->shutdown) {
    return false;
  }
  bool interrupts_masked = mask_interrupts(controller);
  uint8_t status = 0;
  bool reset = reset_stream(controller, &status);
  uint64_t flags = cpu_save_interrupts();
  controller->stream_running = false;
  controller->stream_prepared = false;
  uint8_t errors = controller->irq.errors;
  cpu_restore_interrupts(flags);
  bool dma_disabled = controller->command_ready || set_bus_master(controller, false);
  if (!interrupts_masked || !reset || !dma_disabled) {
    return fail_controller(controller, "output stop/reset or BME disable failed");
  }
  if ((status | errors) & (HDA_SD_FIFO_ERROR | HDA_SD_DESCRIPTOR_ERROR)) {
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

static bool dma_address_fits(const struct dma_buffer *buffer, uint64_t maximum)
{
  return buffer->bytes && !(buffer->physical & (HDA_DMA_ALIGNMENT - 1)) &&
      buffer->physical <= maximum && buffer->bytes - 1 <= maximum - buffer->physical;
}

static enum pci_selection select_controller(size_t *selected, enum hda_model *model)
{
  if (pci_inventory_state() != PCI_INVENTORY_COMPLETE) {
    return PCI_SELECTION_INCOMPLETE;
  }
  bool found = false;
  for (size_t i = 0; i < pci_device_count(); ++i) {
    const struct pci_device *device = pci_device_at(i);
    enum hda_model candidate;
    if (device->vendor_id == HDA_QEMU_VENDOR && device->device_id == HDA_QEMU_DEVICE) {
      candidate = HDA_MODEL_QEMU;
    } else if (device->vendor_id == HDA_AMD_VENDOR && device->device_id == HDA_AMD_DEVICE &&
        device->base_class == HDA_PCI_CLASS && device->subclass == HDA_PCI_SUBCLASS &&
        device->interface == HDA_PCI_INTERFACE &&
        pci_read16(device->address, HDA_PCI_SUBSYSTEM_VENDOR) == HDA_LENOVO_VENDOR &&
        pci_read16(device->address, HDA_PCI_SUBSYSTEM_DEVICE) == HDA_LENOVO_DEVICE) {
      candidate = HDA_MODEL_AMD;
    } else {
      continue;
    }
    if (found) {
      return PCI_SELECTION_AMBIGUOUS;
    }
    found = true;
    *selected = i;
    *model = candidate;
  }
  return found ? PCI_SELECTION_UNIQUE : PCI_SELECTION_ABSENT;
}

static bool enable_native_snoop(struct hda_controller *controller)
{
  struct pci_address address = controller->claim.device->address;
  uint8_t control = pci_read8(address, HDA_AMD_SNOOP_CONTROL);
  uint8_t expected = (control & ~HDA_AMD_SNOOP_MASK) | HDA_AMD_SNOOP_ENABLE;
  pci_write8(&controller->claim, HDA_AMD_SNOOP_CONTROL, expected);
  return pci_read8(address, HDA_AMD_SNOOP_CONTROL) == expected;
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
  ktrace("hda: boot PCI=%u:%u.%u COMMAND=%x Mem=%u BME=%u\n",
      (unsigned)address.bus, (unsigned)address.device, (unsigned)address.function,
      (unsigned)state.command, !!(state.command & PCI_COMMAND_MEMORY),
      !!(state.command & PCI_COMMAND_MASTER));
  if (state.power == HDA_BOOT_POWER_PRESENT) {
    state.pmcsr = pci_read16(address, state.power_capability + PCI_POWER_CONTROL);
    ktrace("hda: boot PM-cap=%x PMCSR=%x D%u NoSoftRst=%u PME-enable=%u PME-status=%u\n",
        state.power_capability, (unsigned)state.pmcsr,
        (unsigned)(state.pmcsr & PCI_POWER_STATE_MASK),
        !!(state.pmcsr & PCI_POWER_NO_SOFT_RESET), !!(state.pmcsr & PCI_POWER_PME_ENABLE),
        !!(state.pmcsr & PCI_POWER_PME_STATUS));
  } else {
    ktrace("hda: boot PM capability %s; power state unavailable\n",
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
  enum pci_selection selection = select_controller(&selected, &controller->model);
  if (selection != PCI_SELECTION_UNIQUE) {
    if (selection == PCI_SELECTION_ABSENT) {
      ktrace("hda: supported controller absent; inactive\n");
    } else {
      klog("hda: controller selection=%u; inactive\n", (unsigned)selection);
    }
    return;
  }
  struct hda_boot_state boot_state = capture_boot_state(pci_device_at(selected));
  if (!pci_reserve_device_at(selected, &controller->claim)) {
    klog("hda: cannot reserve controller; inactive\n");
    return;
  }
  struct pci_device *device = controller->claim.device;
  bool probe_ready = pci_begin_mmio_probe(&controller->claim, &controller->firmware);
  const char *probe_failure = "BAR0 bootstrap unavailable";
  if (!probe_ready && controller->model == HDA_MODEL_AMD) {
    probe_failure = native_probe_failure(&boot_state);
  }
  if (!probe_ready ||
      pci_map_bootstrap_bar(&controller->claim, 0, boot, &controller->registers) != MM_OK) {
    if (pci_restore_mmio_probe(&controller->claim, &controller->firmware)) {
      pci_cancel_reservation(&controller->claim);
      klog("hda: %s; inactive\n", probe_failure);
    } else {
      controller->failed = true;
      klog("hda: %s; probe restore failed; reservation retained until reboot\n", probe_failure);
    }
    return;
  }
  /* Halt firmware engines before claim completion changes BME/INTx or sizes BARs.
   * No driver DMA is published here; link release belongs to the worker. */
  if (!boot_halt(controller) || !pci_complete_claim(&controller->claim)) {
    controller->failed = true;
    klog("hda: boot halt/claim failed; ownership retained until reboot\n");
    return;
  }
  uint16_t command = pci_read16(device->address, PCI_COMMAND);
  pci_write16(&controller->claim, PCI_COMMAND, command & ~(PCI_COMMAND_IO | PCI_COMMAND_MEMORY));
  if (pci_read16(device->address, PCI_COMMAND) &
      (PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER)) {
    controller->failed = true;
    klog("hda: BAR decoding disable failed; ownership retained until reboot\n");
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
    klog("hda: BAR0 extent rejected; ownership retained until reboot\n");
    return;
  }
  uint16_t capabilities = read16(controller, HDA_GCAP);
  unsigned input = (capabilities >> HDA_GCAP_ISS_SHIFT) & HDA_GCAP_STREAM_COUNT_MASK;
  unsigned output = (capabilities >> HDA_GCAP_OSS_SHIFT) & HDA_GCAP_STREAM_COUNT_MASK;
  controller->stream = HDA_STREAM_BASE + input * HDA_STREAM_BYTES;
  ktrace("hda: PCI=%u:%u.%u GCAP=%x ISS=%u OSS=%u version=%u.%u\n",
      (unsigned)device->address.bus, (unsigned)device->address.device,
      (unsigned)device->address.function, (unsigned)capabilities, input, output,
      (unsigned)read8(controller, HDA_VMAJ), (unsigned)read8(controller, HDA_VMIN));
  if (controller->model == HDA_MODEL_AMD && !enable_native_snoop(controller)) {
    controller->failed = true;
    klog("hda: native coherent DMA snoop did not set; ownership retained until reboot\n");
    return;
  }
  if (!output || controller->stream + HDA_STREAM_BYTES > PCI_BOOTSTRAP_BAR_BYTES ||
      dma_buffer_allocate(&controller->corb, PAGE_SIZE) != MM_OK ||
      dma_buffer_allocate(&controller->rirb, PAGE_SIZE) != MM_OK ||
      dma_buffer_allocate(&controller->bdl, PAGE_SIZE) != MM_OK ||
      dma_buffer_allocate(&controller->pcm, HDA_BUFFER_BYTES) != MM_OK) {
    controller->failed = true;
    klog("hda: output or coherent DMA unavailable; backing retained until reboot\n");
    return;
  }
  uint64_t maximum = UINT32_MAX;
  if (capabilities & HDA_GCAP_64OK) {
    maximum = controller->model == HDA_MODEL_AMD ? HDA_AMD_DMA_MAX : UINT64_MAX;
  }
  if (!dma_address_fits(&controller->corb, maximum) ||
      !dma_address_fits(&controller->rirb, maximum) ||
      !dma_address_fits(&controller->bdl, maximum) ||
      !dma_address_fits(&controller->pcm, maximum)) {
    controller->failed = true;
    klog("hda: DMA exceeds controller address width; backing retained until reboot\n");
    return;
  }
  if (!pci_msi_prepare(&controller->claim, &controller->msi, APIC_HDA_VECTOR)) {
    controller->failed = true;
    klog("hda: single-message MSI unavailable; ownership retained until reboot\n");
    return;
  }
  controller->prepared = true;
  ktrace("hda: prepared output descriptor=%u PCM=%u backing=%zu; link reset, BME off\n",
      input, HDA_BUFFER_BYTES, controller->pcm.bytes);
}
