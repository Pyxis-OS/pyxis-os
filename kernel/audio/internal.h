#ifndef KERNEL_AUDIO_INTERNAL_H
#define KERNEL_AUDIO_INTERNAL_H

#include <kernel/boot.h>
#include <kernel/audio.h>
#include <kernel/mm/dma.h>
#include <kernel/pci.h>
#include <kernel/pci/msi.h>

#define HDA_RATE 48000
#define HDA_FRAME_BYTES 4
#define HDA_PERIOD_FRAMES AUDIO_PERIOD_FRAMES
#define HDA_PERIOD_BYTES (HDA_PERIOD_FRAMES * HDA_FRAME_BYTES)
#define HDA_PERIOD_COUNT 8
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

/* QEMU keeps its measured emulation profile and generic codec route search.
 * Native controllers use the FIFO-based timing profile and exact codec profiles. */
enum hda_model { HDA_MODEL_QEMU, HDA_MODEL_NATIVE };

/* How DMA is kept snooped by the CPU caches; see hda.md. */
enum hda_coherence { HDA_COHERENCE_NONE, HDA_COHERENCE_PCIE, HDA_COHERENCE_INTEL_DEVC };

/* One entry of the controller quirk table. Unlisted HDA-class controllers
 * get the native defaults: PCIe No Snoop clear and command rings. */
struct hda_controller_profile {
  const char *name;
  uint16_t vendor, device;
  bool any_device;
  /* Intel DSP-capable controllers report audio subclass 0x01, not HDA. */
  uint8_t subclass;
  enum hda_model model;
  enum hda_coherence coherence;
  /* Inclusive DMA ceiling when GCAP reports 64-bit addressing. */
  uint64_t dma_maximum;
  bool clear_tcsel, ungate_clocks_for_reset, immediate_commands;
};

struct hda_controller {
  enum hda_model model;
  const struct hda_controller_profile *profile;
  struct pci_claim claim;
  struct pci_mapping registers;
  struct pci_probe_state firmware;
  struct pci_msi msi;
  struct dma_buffer corb, rirb, bdl, pcm;
  unsigned stream, corb_entries, rirb_entries;
  uint16_t corb_write, rirb_read, codec_mask;
  uint64_t commands, responses, unsolicited;
  uint64_t run_time;
  uint32_t run_wallclock;
  uint32_t fifo_bytes;
  /* Why the last RUN or position observation was refused, recorded at IF=0
   * for the worker to log; register values as last read. */
  const char *start_failure;
  uint32_t start_intctl, start_lpib[2];
  /* SDnCTL bytes 0-2 with SDnSTS in byte 3, read as one dword. */
  uint32_t start_ctl;
  /* How long the last successful RUN took to read back. */
  uint64_t run_latch_ns;
  struct hda_irq_event irq;
  bool prepared, link_ready, command_ready, stream_prepared, stream_running, failed, shutdown;
  /* During an initialization attempt hda_fail() only records the first reason;
   * the worker then resets the link and retries, or fails with that reason. */
  bool setup_attempt;
  const char *setup_failure;
};

struct hda_route {
  uint32_t vendor, revision, subsystem;
  uint8_t codec, group, pin, converter, headphone_pin, headphone_converter;
  unsigned length;
};

enum hda_position_result {
  HDA_POSITION_STABLE,
  HDA_POSITION_MOVING,
  HDA_POSITION_FAILED,
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
/* After a failed initialization attempt: masks delivery, stops the command
 * rings, asserts link reset and disables bus mastering, checking each step.
 * False means ownership is uncertain and the engine must fail. */
bool hda_link_reset(struct hda_controller *controller);
bool hda_commands_start(struct hda_controller *controller);
bool hda_commands_stop(struct hda_controller *controller);
bool hda_command(struct hda_controller *controller, uint8_t codec, uint8_t node,
    uint32_t verb_payload, uint32_t *response);
bool hda_stream_prepare(struct hda_controller *controller);
/* BSP/IF=0, no waits or failure shutdown. The worker fills PCM after prepare,
 * then publishes it and RUN without cancellation between consumption and RUN.
 * A false return requires worker-context quarantine before any further use. */
bool hda_stream_run_locked(struct hda_controller *controller);
/* BSP/IF=0, no sleeps or allocation. Locked observation keeps the oldest pending
 * notification and sticky errors; the engine consumes completion/time with each
 * accepted position under the same IF=0 boundary.
 * MOVING is a validated, running, error-free stream with two in-range aligned
 * positions after the fixed sample budget; it supplies no accepted progress.
 * The engine must discard that epoch through bounded timing recovery.
 * The engine owns progress/epoch checks before committing a reclaimed period. */
enum hda_position_result hda_stream_position_locked(struct hda_controller *controller,
    struct hda_stream_position *position, struct hda_irq_event *event);
/* BSP/IF=0 and RUN active; bracket native position scans without changing
 * completion ownership. This wrapping clock does not count consumed bytes. */
uint32_t hda_stream_wallclock_locked(struct hda_controller *controller);
bool hda_interrupt(struct hda_controller *controller);
bool hda_stream_stop(struct hda_controller *controller);
/* Stop and reset after a refused start. FIFO/descriptor errors are returned
 * rather than failing the controller; false means the stop itself failed. */
bool hda_stream_abort(struct hda_controller *controller, uint8_t *errors);
void hda_shutdown(struct hda_controller *controller);
/* Owning worker after a refused start, before the stream is reset: log the
 * controller state that decides whether a stream can run. */
void hda_log_start_state(struct hda_controller *controller);
void hda_fail(struct hda_controller *controller, const char *reason);

/* Discovery keeps one checked route; activation is separate and happens only
 * when a stream is requested. These are private engine helpers, not an audio ABI. */
bool hda_codec_discover(struct hda_controller *controller, struct hda_route *route);
bool hda_codec_enable(struct hda_controller *controller, const struct hda_route *route);
/* Native normal-stop first phase, owning IF-enabled worker only. Mutes both
 * outputs without changing pin/EAPD controls; the worker owns settle deadlines. */
bool hda_codec_stop_mute(struct hda_controller *controller, const struct hda_route *route,
    bool *headphone_active);
bool hda_codec_disable(struct hda_controller *controller, const struct hda_route *route);

#endif
