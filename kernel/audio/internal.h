#ifndef KERNEL_AUDIO_INTERNAL_H
#define KERNEL_AUDIO_INTERNAL_H

#include <kernel/boot.h>
#include <kernel/mm/dma.h>
#include <kernel/pci.h>

#define HDA_RATE 48000
#define HDA_FRAME_BYTES 4
#define HDA_PERIOD_FRAMES 480
#define HDA_PERIOD_BYTES (HDA_PERIOD_FRAMES * HDA_FRAME_BYTES)
#define HDA_PERIOD_COUNT 4
#define HDA_BUFFER_BYTES (HDA_PERIOD_BYTES * HDA_PERIOD_COUNT)
#define HDA_STREAM_FORMAT 0x0011
#define HDA_STREAM_TAG 1

struct hda_controller {
  struct pci_claim claim;
  struct pci_mapping registers;
  struct pci_probe_state firmware;
  struct dma_buffer corb, rirb, bdl, pcm;
  unsigned stream, corb_entries, rirb_entries;
  uint16_t corb_write, rirb_read, codec_mask;
  uint64_t commands, responses, unsolicited;
  bool prepared, link_ready, command_ready, stream_running, failed, shutdown;
};

struct hda_route {
  uint32_t vendor, revision;
  uint8_t codec, group, pin, converter;
  unsigned length;
};

struct hda_stream_position {
  uint32_t bytes;
  bool completed;
};

/* All runtime helpers require the owning IF-enabled BSP worker. DMA allocations
 * and PCI/MMIO ownership are prepared before AP startup and retained until reboot. */
void audio_require_worker(void);
void hda_prepare(struct hda_controller *controller, const struct boot_info *boot);
bool hda_link_start(struct hda_controller *controller);
bool hda_commands_start(struct hda_controller *controller);
bool hda_commands_stop(struct hda_controller *controller);
bool hda_command(struct hda_controller *controller, uint8_t codec, uint8_t node,
    uint32_t verb_payload, uint32_t *response);
bool hda_stream_start(struct hda_controller *controller, const void *pcm, size_t bytes);
bool hda_stream_position(struct hda_controller *controller, struct hda_stream_position *position);
bool hda_stream_stop(struct hda_controller *controller);
void hda_shutdown(struct hda_controller *controller);
void hda_fail(struct hda_controller *controller, const char *reason);

/* Discovery keeps one checked route; activation is separate and happens only
 * when a stream is requested. These are private engine helpers, not an audio ABI. */
bool hda_codec_discover(struct hda_controller *controller, struct hda_route *route);
bool hda_codec_enable(struct hda_controller *controller, const struct hda_route *route);
bool hda_codec_disable(struct hda_controller *controller, const struct hda_route *route);

#endif
