#ifndef USB_BOT_H
#define USB_BOT_H

#include "host.h"

#define USB_BOT_SAMPLE_BYTES 64

enum usb_bot_state {
  USB_BOT_UNBOUND, USB_BOT_UNSUPPORTED, USB_BOT_FAILED, USB_BOT_READY
};

/* Worker-owned boot probe facts, with no block device or public read interface. */
struct usb_bot {
  struct usb_host_device *host;
  struct usb_bulk_endpoint in, out;
  enum usb_bot_state state;
  const char *detail;
  uint8_t configuration, interface, alternate;
  uint32_t tag, block_bytes;
  uint64_t blocks, read_bytes;
  uint32_t commands, reads, recoveries;
  bool wide, sense_valid;
  uint8_t sense_response, sense_key, sense_asc, sense_ascq;
  uint8_t first_sample[USB_BOT_SAMPLE_BYTES], last_sample[USB_BOT_SAMPLE_BYTES];
};

/* Called only after the core validates the complete configuration. The first
 * supported single-interface SCSI BOT candidate wins across configurations. */
bool usb_bot_select(struct usb_bot *bot, const uint8_t *descriptors, size_t total,
                    enum usb_speed speed);
/* Scratch belongs to the caller and remains valid for this synchronous probe.
 * Inspection completeness and device admission are the core's responsibility. */
bool usb_bot_probe(struct usb_bot *bot, void *scratch, size_t capacity, uint64_t overall);

#endif
