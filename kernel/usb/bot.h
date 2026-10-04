#ifndef USB_BOT_H
#define USB_BOT_H

#include "host.h"

#define USB_BOT_SAMPLE_BYTES 64

enum usb_bot_state {
  USB_BOT_UNBOUND, USB_BOT_UNSUPPORTED, USB_BOT_FAILED, USB_BOT_READY
};

enum usb_bot_io_result {
  USB_BOT_IO_OK, USB_BOT_IO_CLEAN_REJECTED, USB_BOT_IO_FAILED, USB_BOT_IO_TIMED_OUT
};

/* Controller-worker-owned media facts, sense and terminal failure. */
struct usb_bot {
  struct usb_host_device *host;
  struct usb_bulk_endpoint in, out;
  enum usb_bot_state state;
  const char *detail, *write_detail;
  uint8_t configuration, interface, alternate;
  uint32_t tag, block_bytes;
  uint64_t blocks, read_bytes, write_bytes;
  uint32_t commands, reads, writes, flushes, recoveries;
  bool wide, sense_valid, timed_out, writable, flush_supported;
  bool write_protect_known, write_protected;
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

/* Owning controller worker after geometry setup, during probe or READY service.
 * A clean rejection retains valid sense and leaves the channel usable; it never
 * retries I/O. Scratch is private captured storage and may change on error.
 * submitted becomes true exactly when the original I/O CBW is first published.
 * WRITE and FLUSH require the successfully qualified READY media profile. */
enum usb_bot_io_result usb_bot_read(struct usb_bot *bot, uint64_t first_block, uint32_t block_count,
    void *scratch, size_t scratch_bytes, uint64_t deadline, bool *submitted);
enum usb_bot_io_result usb_bot_write(struct usb_bot *bot, uint64_t first_block, uint32_t block_count,
    void *scratch, size_t scratch_bytes, uint64_t deadline, bool *submitted);
enum usb_bot_io_result usb_bot_flush(struct usb_bot *bot, void *scratch, size_t scratch_bytes,
    uint64_t deadline, bool *submitted);

#endif
