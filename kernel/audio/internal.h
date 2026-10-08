#ifndef KERNEL_AUDIO_INTERNAL_H
#define KERNEL_AUDIO_INTERNAL_H

#include <kernel/boot.h>
#include <kernel/mm/dma.h>
#include <kernel/pci.h>
#include <kernel/pci/msi.h>

#define HDA_RATE 48000
#define HDA_FRAME_BYTES 4
#define HDA_PERIOD_FRAMES 480
#define HDA_PERIOD_BYTES (HDA_PERIOD_FRAMES * HDA_FRAME_BYTES)
#define HDA_PERIOD_COUNT 4
#define HDA_BUFFER_BYTES (HDA_PERIOD_BYTES * HDA_PERIOD_COUNT)
#define HDA_STREAM_FORMAT 0x0011
#define HDA_STREAM_TAG 1
#define HDA_IRQ_FIFO_ERROR (1u << 3)
#define HDA_IRQ_DESCRIPTOR_ERROR (1u << 4)

struct hda_irq_event {
  /* Monotonic ns of the oldest notification not consumed by the worker. */
  uint64_t first_time;
  uint8_t errors;
  bool completed;
};

struct hda_controller {
  struct pci_claim claim;
  struct pci_mapping registers;
  struct pci_probe_state firmware;
  struct pci_msi msi;
  struct dma_buffer corb, rirb, bdl, pcm;
  unsigned stream, corb_entries, rirb_entries;
  uint16_t corb_write, rirb_read, codec_mask;
  uint64_t commands, responses, unsolicited;
  struct hda_irq_event irq;
  bool prepared, link_ready, command_ready, stream_prepared, stream_running, failed, shutdown;
};

struct hda_route {
  uint32_t vendor, revision;
  uint8_t codec, group, pin, converter;
  unsigned length;
};

struct hda_stream_position {
  uint32_t bytes, wallclock;
  bool completed;
};

/* Runtime helpers require the owning IF-enabled BSP worker unless documented
 * below. DMA allocations and PCI/MMIO ownership are prepared before AP startup
 * and retained until reboot. */
void audio_require_worker(void);
void hda_prepare(struct hda_controller *controller, const struct boot_info *boot);
bool hda_link_start(struct hda_controller *controller);
bool hda_commands_start(struct hda_controller *controller);
bool hda_commands_stop(struct hda_controller *controller);
bool hda_command(struct hda_controller *controller, uint8_t codec, uint8_t node,
    uint32_t verb_payload, uint32_t *response);
bool hda_stream_prepare(struct hda_controller *controller);
/* BSP/IF=0, no waits or failure shutdown. The worker fills PCM after prepare,
 * then publishes it and RUN without cancellation between consumption and RUN.
 * A false return requires worker-context quarantine before any further use. */
bool hda_stream_run_locked(struct hda_controller *controller);
bool hda_stream_observe(struct hda_controller *controller, struct hda_stream_position *position,
    struct hda_irq_event *event);
/* BSP/IF=0, no sleeps or allocation. Locked observation keeps the oldest pending
 * notification and sticky errors; only worker observe consumes completion/time.
 * The engine owns progress/epoch checks before committing a reclaimed period. */
bool hda_stream_position_locked(struct hda_controller *controller,
    struct hda_stream_position *position, struct hda_irq_event *event);
bool hda_stream_write_period_locked(struct hda_controller *controller, unsigned period,
    const void *pcm);
bool hda_interrupt(struct hda_controller *controller);
bool hda_stream_stop(struct hda_controller *controller);
void hda_shutdown(struct hda_controller *controller);
void hda_fail(struct hda_controller *controller, const char *reason);

/* Discovery keeps one checked route; activation is separate and happens only
 * when a stream is requested. These are private engine helpers, not an audio ABI. */
bool hda_codec_discover(struct hda_controller *controller, struct hda_route *route);
bool hda_codec_enable(struct hda_controller *controller, const struct hda_route *route);
bool hda_codec_disable(struct hda_controller *controller, const struct hda_route *route);

#endif
