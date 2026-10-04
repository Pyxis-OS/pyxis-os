#include <kernel/memory.h>
#include <kernel/task.h>
#include <limits.h>
#include "bot.h"
#include "settings.h"

#define BOT_DESCRIPTOR_CONFIGURATION 2
#define BOT_DESCRIPTOR_INTERFACE 4
#define BOT_DESCRIPTOR_ENDPOINT 5
#define BOT_DESCRIPTOR_SUPER_COMPANION 48
#define BOT_CLASS_STORAGE 0x08
#define BOT_SUBCLASS_SCSI 0x06
#define BOT_PROTOCOL_BULK_ONLY 0x50
#define BOT_ENDPOINT_IN 0x80
#define BOT_ENDPOINT_TYPE_MASK 0x03
#define BOT_ENDPOINT_BULK 0x02
#define BOT_REQUEST_SET_CONFIGURATION 9
#define BOT_REQUEST_SET_INTERFACE 11
#define BOT_REQUEST_GET_MAX_LUN 0xfe
#define BOT_REQUEST_RESET 0xff
#define BOT_REQUEST_DEVICE_OUT 0x00
#define BOT_REQUEST_INTERFACE_OUT 0x01
#define BOT_REQUEST_CLASS_INTERFACE_OUT 0x21
#define BOT_REQUEST_CLASS_INTERFACE_IN 0xa1
#define BOT_CBW_BYTES 31
#define BOT_CSW_BYTES 13
#define BOT_CBW_SIGNATURE 0x43425355u
#define BOT_CSW_SIGNATURE 0x53425355u
#define BOT_CBW_DATA_IN 0x80
#define BOT_CSW_PASSED 0
#define BOT_CSW_FAILED 1
#define BOT_CSW_PHASE_ERROR 2
#define BOT_MAX_LUN 15
#define BOT_READY_ATTEMPTS 3
#define SCSI_TEST_UNIT_READY 0x00
#define SCSI_REQUEST_SENSE 0x03
#define SCSI_INQUIRY 0x12
#define SCSI_READ_CAPACITY_10 0x25
#define SCSI_READ_10 0x28
#define SCSI_READ_16 0x88
#define SCSI_SERVICE_ACTION_IN_16 0x9e
#define SCSI_READ_CAPACITY_16_ACTION 0x10
#define SCSI_INQUIRY_BYTES 36
#define SCSI_SENSE_BYTES 18
#define SCSI_CAPACITY_10_BYTES 8
#define SCSI_CAPACITY_16_BYTES 32
#define SCSI_DEVICE_TYPE_MASK 0x1f
#define SCSI_QUALIFIER_SHIFT 5
#define SCSI_DEVICE_DIRECT_ACCESS 0
#define SCSI_SENSE_FIXED_CURRENT 0x70
#define SCSI_SENSE_FIXED_DEFERRED 0x71
#define SCSI_SENSE_DESCRIPTOR_CURRENT 0x72
#define SCSI_SENSE_DESCRIPTOR_DEFERRED 0x73
#define SCSI_SENSE_RESPONSE_MASK 0x7f
#define SCSI_SENSE_KEY_MASK 0x0f
#define SCSI_SENSE_UNIT_ATTENTION 0x06
#define SCSI_CAPACITY_PROTECTION_ENABLED 0x01
#define BOT_BLOCK_SMALL 512
#define BOT_BLOCK_LARGE 4096

enum bot_command_result { BOT_COMMAND_OK, BOT_COMMAND_REJECTED, BOT_COMMAND_BROKEN };

static uint16_t little16(const uint8_t *bytes)
{
  return bytes[0] | ((uint16_t)bytes[1] << 8);
}

static uint32_t little32(const uint8_t *bytes)
{
  return little16(bytes) | ((uint32_t)little16(bytes + 2) << 16);
}

static void write_little32(uint8_t *bytes, uint32_t value)
{
  for (unsigned i = 0; i < 4; ++i) {
    bytes[i] = value >> (i * 8);
  }
}

static uint32_t big32(const uint8_t *bytes)
{
  return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
         ((uint32_t)bytes[2] << 8) | bytes[3];
}

static uint64_t big64(const uint8_t *bytes)
{
  return ((uint64_t)big32(bytes) << 32) | big32(bytes + 4);
}

static void write_big(uint8_t *bytes, uint64_t value, unsigned count)
{
  for (unsigned i = 0; i < count; ++i) {
    bytes[count - i - 1] = value >> (i * 8);
  }
}

static uint64_t bounded_deadline(uint64_t overall, uint32_t milliseconds)
{
  uint64_t deadline = task_deadline_after_ms(milliseconds);
  return deadline < overall ? deadline : overall;
}

static bool fail(struct usb_bot *bot, const char *detail)
{
  bot->state = USB_BOT_FAILED;
  bot->detail = detail;
  return false;
}

static bool unsupported(struct usb_bot *bot, const char *detail)
{
  bot->state = USB_BOT_UNSUPPORTED;
  bot->detail = detail;
  return false;
}

static enum usb_result control(struct usb_bot *bot, const struct usb_setup *setup,
                               void *destination, size_t capacity, size_t *bytes,
                               uint64_t deadline)
{
  *bytes = 0;
  if (task_deadline_expired(deadline)) {
    return USB_TIMEOUT;
  }
  struct usb_ticket ticket;
  enum usb_result result = usb_host_control_submit(bot->host, setup, NULL, deadline, &ticket);
  if (result != USB_OK) {
    return result;
  }
  result = usb_host_control_wait(bot->host, ticket, deadline);
  if (result != USB_OK) {
    usb_host_control_abandon(bot->host, ticket);
    return result;
  }
  struct usb_completion completion;
  result = usb_host_control_take(bot->host, ticket, destination, capacity, &completion);
  if (result != USB_OK) {
    usb_host_control_abandon(bot->host, ticket);
    return result;
  }
  *bytes = completion.bytes;
  return completion.result;
}

static bool candidate(struct usb_bot *bot, const uint8_t *descriptors, size_t start,
                      size_t end, enum usb_speed speed)
{
  const uint8_t *interface = descriptors + start;
  if (interface[4] != 2 || interface[5] != BOT_CLASS_STORAGE ||
      interface[6] != BOT_SUBCLASS_SCSI || interface[7] != BOT_PROTOCOL_BULK_ONLY) {
    return false;
  }
  struct usb_bulk_endpoint in = {0}, out = {0};
  unsigned endpoints = 0;
  for (size_t offset = start + interface[0]; offset < end; offset += descriptors[offset]) {
    const uint8_t *part = descriptors + offset;
    if (part[1] != BOT_DESCRIPTOR_ENDPOINT) {
      continue;
    }
    if ((part[3] & BOT_ENDPOINT_TYPE_MASK) != BOT_ENDPOINT_BULK || ++endpoints > 2) {
      return false;
    }
    struct usb_bulk_endpoint endpoint = {
      .address = part[2], .packet = little16(part + 4)
    };
    if (usb_speed_is_enhanced(speed)) {
      size_t companion = offset + part[0];
      if (companion >= end || descriptors[companion + 1] != BOT_DESCRIPTOR_SUPER_COMPANION ||
          descriptors[companion + 3]) {
        return false;
      }
      endpoint.burst = descriptors[companion + 2];
    }
    struct usb_bulk_endpoint *selected = endpoint.address & BOT_ENDPOINT_IN ? &in : &out;
    if (selected->address) {
      return false;
    }
    *selected = endpoint;
  }
  if (endpoints != 2 || !in.address || !out.address) {
    return false;
  }
  bot->configuration = descriptors[5];
  bot->interface = interface[2];
  bot->alternate = interface[3];
  bot->in = in;
  bot->out = out;
  bot->state = USB_BOT_UNBOUND;
  bot->detail = "selected boot SCSI BOT interface";
  return true;
}

bool usb_bot_select(struct usb_bot *bot, const uint8_t *descriptors, size_t total,
                    enum usb_speed speed)
{
  if (bot->configuration) {
    return true;
  }
  if (total < 9 || descriptors[1] != BOT_DESCRIPTOR_CONFIGURATION) {
    return false;
  }
  size_t interface = 0;
  for (size_t offset = descriptors[0]; offset < total; offset += descriptors[offset]) {
    if (descriptors[offset + 1] != BOT_DESCRIPTOR_INTERFACE) {
      continue;
    }
    if (descriptors[offset + 5] == BOT_CLASS_STORAGE) {
      unsupported(bot, "storage interface outside supported single-interface SCSI BOT contract");
    }
    if (interface && descriptors[4] == 1 && candidate(bot, descriptors, interface, offset, speed)) {
      return true;
    }
    interface = offset;
  }
  return interface && descriptors[4] == 1 && candidate(bot, descriptors, interface, total, speed);
}

static bool reset_recovery(struct usb_bot *bot)
{
  ++bot->recoveries;
  /* Recovery has its own budget even when the failed probe exhausted overall. */
  uint64_t deadline = task_deadline_after_ms(USB_BOT_RECOVERY_TIMEOUT_MS);
  struct usb_setup setup = {
    .request_type = BOT_REQUEST_CLASS_INTERFACE_OUT, .request = BOT_REQUEST_RESET,
    .index = bot->interface
  };
  size_t bytes;
  /* BOT reset preserves both halts and toggles. The specified order is reset,
   * clear Bulk-In, then clear Bulk-Out, including endpoints that did not stall. */
  return control(bot, &setup, NULL, 0, &bytes, deadline) == USB_OK &&
         usb_host_bulk_clear(bot->host, bot->in.address, NULL, 0, NULL, deadline) == USB_OK &&
         usb_host_bulk_clear(bot->host, bot->out.address, NULL, 0, NULL, deadline) == USB_OK;
}

static enum bot_command_result broken(struct usb_bot *bot, const char *detail)
{
  fail(bot, reset_recovery(bot) ? detail : "BOT reset recovery failed; device abandoned");
  return BOT_COMMAND_BROKEN;
}

static enum bot_command_result command(struct usb_bot *bot, const uint8_t *cdb,
                                       unsigned cdb_bytes, void *data, size_t length,
                                       size_t *relevant, uint64_t overall, bool *submitted)
{
  bot->timed_out = false;
  *relevant = 0;
  uint64_t deadline = bounded_deadline(overall, USB_BOT_TIMEOUT_MS);
  if (task_deadline_expired(deadline)) {
    bot->timed_out = true;
    fail(bot, "BOT command deadline expired before admission");
    return BOT_COMMAND_BROKEN;
  }
  if (!cdb_bytes || cdb_bytes > 16 || length > USB_BULK_BYTES || (length && !data)) {
    fail(bot, "invalid private BOT command");
    return BOT_COMMAND_BROKEN;
  }
  uint8_t cbw[BOT_CBW_BYTES] = {0};
  uint32_t tag = ++bot->tag;
  write_little32(cbw, BOT_CBW_SIGNATURE);
  write_little32(cbw + 4, tag);
  write_little32(cbw + 8, length);
  cbw[12] = length ? BOT_CBW_DATA_IN : 0;
  cbw[14] = cdb_bytes;
  memcpy(cbw + 15, cdb, cdb_bytes);
  ++bot->commands;
  size_t actual;
  enum usb_result result = usb_host_bulk_transfer(bot->host, bot->out.address, cbw, NULL,
                                                 sizeof(cbw), deadline, &actual, submitted);
  if (result != USB_OK || actual != sizeof(cbw)) {
    bot->timed_out = result == USB_TIMEOUT;
    return broken(bot, "BOT command wrapper transfer failed");
  }
  size_t received = 0;
  if (length) {
    result = usb_host_bulk_transfer(bot->host, bot->in.address, NULL, data, length, deadline, &received, NULL);
    if (result == USB_STALL) {
      received = 0;
      result = usb_host_bulk_clear(bot->host, bot->in.address, data, length, &received, deadline);
      if (result != USB_OK) {
        bot->timed_out = result == USB_TIMEOUT;
        return broken(bot, "BOT data halt could not be cleared");
      }
    } else if (result != USB_OK || received > length) {
      bot->timed_out = result == USB_TIMEOUT;
      return broken(bot, "BOT data transfer failed");
    }
  }
  uint8_t csw[BOT_CSW_BYTES];
  result = usb_host_bulk_transfer(bot->host, bot->in.address, NULL, csw, sizeof(csw), deadline, &actual, NULL);
  if (result == USB_STALL) {
    result = usb_host_bulk_clear(bot->host, bot->in.address, NULL, 0, NULL, deadline);
    if (result != USB_OK) {
      bot->timed_out = result == USB_TIMEOUT;
      return broken(bot, "BOT status halt could not be cleared");
    }
    result = usb_host_bulk_transfer(bot->host, bot->in.address, NULL, csw, sizeof(csw), deadline, &actual, NULL);
  }
  if (result != USB_OK || actual != sizeof(csw) || little32(csw) != BOT_CSW_SIGNATURE ||
      little32(csw + 4) != tag) {
    bot->timed_out = result == USB_TIMEOUT;
    return broken(bot, "invalid BOT status wrapper");
  }
  uint32_t residue = little32(csw + 8);
  unsigned status = csw[12];
  if (status == BOT_CSW_PHASE_ERROR || status > BOT_CSW_PHASE_ERROR || residue > length) {
    return broken(bot, "BOT phase error or meaningless status");
  }
  if (status == BOT_CSW_FAILED) {
    return BOT_COMMAND_REJECTED;
  }
  /* Data padding can exceed the relevant bytes reported by the CSW. A cleared
   * data halt may return partial bytes only after the host safely retires DMA. */
  size_t valid = length - residue;
  if (valid > received) {
    return broken(bot, "BOT residue exceeds received data");
  }
  *relevant = valid;
  return BOT_COMMAND_OK;
}

static bool request_sense(struct usb_bot *bot, void *scratch, uint64_t overall)
{
  uint8_t cdb[6] = { SCSI_REQUEST_SENSE, 0, 0, 0, SCSI_SENSE_BYTES, 0 };
  size_t bytes;
  bot->sense_valid = false;
  if (command(bot, cdb, sizeof(cdb), scratch, SCSI_SENSE_BYTES, &bytes, overall, NULL) != BOT_COMMAND_OK) {
    if (bot->state != USB_BOT_FAILED) {
      fail(bot, "SCSI REQUEST SENSE failed");
    }
    return false;
  }
  const uint8_t *sense = scratch;
  if (bytes < 8) {
    return fail(bot, "truncated SCSI sense header");
  }
  bot->sense_response = sense[0] & SCSI_SENSE_RESPONSE_MASK;
  size_t declared = 8u + sense[7];
  if (bot->sense_response == SCSI_SENSE_FIXED_CURRENT ||
      bot->sense_response == SCSI_SENSE_FIXED_DEFERRED) {
    if (bytes < 14 || declared < 14) {
      return fail(bot, "truncated fixed SCSI sense data");
    }
    bot->sense_key = sense[2] & SCSI_SENSE_KEY_MASK;
    bot->sense_asc = sense[12];
    bot->sense_ascq = sense[13];
  } else if (bot->sense_response == SCSI_SENSE_DESCRIPTOR_CURRENT ||
             bot->sense_response == SCSI_SENSE_DESCRIPTOR_DEFERRED) {
    bot->sense_key = sense[1] & SCSI_SENSE_KEY_MASK;
    bot->sense_asc = sense[2];
    bot->sense_ascq = sense[3];
  } else {
    return fail(bot, "unsupported SCSI sense response format");
  }
  bot->sense_valid = true;
  return true;
}

static bool require_command(struct usb_bot *bot, const uint8_t *cdb, unsigned cdb_bytes,
                            void *scratch, size_t length, size_t *bytes, uint64_t overall)
{
  enum bot_command_result result = command(bot, cdb, cdb_bytes, scratch, length, bytes, overall, NULL);
  if (result == BOT_COMMAND_OK) {
    return true;
  }
  if (result == BOT_COMMAND_REJECTED && request_sense(bot, scratch, overall)) {
    fail(bot, "SCSI command rejected; sense retained");
  }
  return false;
}

static bool ready(struct usb_bot *bot, void *scratch, uint64_t overall)
{
  uint8_t cdb[6] = { SCSI_TEST_UNIT_READY };
  for (unsigned attempt = 0; attempt < BOT_READY_ATTEMPTS; ++attempt) {
    size_t bytes;
    enum bot_command_result result = command(bot, cdb, sizeof(cdb), NULL, 0, &bytes, overall, NULL);
    if (result == BOT_COMMAND_OK) {
      return true;
    }
    if (result == BOT_COMMAND_BROKEN || !request_sense(bot, scratch, overall)) {
      return false;
    }
    if (bot->sense_response != SCSI_SENSE_FIXED_CURRENT &&
        bot->sense_response != SCSI_SENSE_DESCRIPTOR_CURRENT) {
      return fail(bot, "SCSI deferred error; sense retained");
    }
    if (bot->sense_key != SCSI_SENSE_UNIT_ATTENTION) {
      return fail(bot, "SCSI medium not ready; sense retained");
    }
  }
  return fail(bot, "SCSI unit attention persisted beyond readiness budget");
}

static bool capacity(struct usb_bot *bot, void *scratch, uint64_t overall)
{
  uint8_t cdb[16] = { SCSI_READ_CAPACITY_10 };
  size_t bytes;
  if (!require_command(bot, cdb, 10, scratch, SCSI_CAPACITY_10_BYTES, &bytes, overall)) {
    return false;
  }
  if (bytes != SCSI_CAPACITY_10_BYTES) {
    return fail(bot, "short SCSI READ CAPACITY (10) response");
  }
  const uint8_t *data = scratch;
  uint64_t last = big32(data);
  bot->block_bytes = big32(data + 4);
  if (last == UINT32_MAX) {
    memset(cdb, 0, sizeof(cdb));
    cdb[0] = SCSI_SERVICE_ACTION_IN_16;
    cdb[1] = SCSI_READ_CAPACITY_16_ACTION;
    write_big(cdb + 10, SCSI_CAPACITY_16_BYTES, 4);
    if (!require_command(bot, cdb, sizeof(cdb), scratch, SCSI_CAPACITY_16_BYTES, &bytes, overall)) {
      return false;
    }
    if (bytes != SCSI_CAPACITY_16_BYTES) {
      return fail(bot, "short SCSI READ CAPACITY (16) response");
    }
    if (data[12] & SCSI_CAPACITY_PROTECTION_ENABLED) {
      return unsupported(bot, "SCSI protection information geometry is deferred");
    }
    last = big64(data);
    bot->block_bytes = big32(data + 8);
    bot->wide = true;
  }
  if (last == UINT64_MAX) {
    return fail(bot, "SCSI capacity count overflows");
  }
  bot->blocks = last + 1;
  if (bot->block_bytes != BOT_BLOCK_SMALL && bot->block_bytes != BOT_BLOCK_LARGE) {
    return unsupported(bot, "SCSI block size outside 512/4096-byte probe policy");
  }
  if (bot->blocks > UINT64_MAX / bot->block_bytes) {
    return unsupported(bot, "SCSI byte capacity exceeds bounded geometry");
  }
  return true;
}

enum usb_bot_read_result usb_bot_read(struct usb_bot *bot, uint64_t lba, uint32_t count, void *scratch,
    size_t scratch_bytes, uint64_t overall, bool *submitted)
{
  if (!bot || !bot->host || !bot->configuration || !scratch ||
      bot->state == USB_BOT_FAILED || bot->state == USB_BOT_UNSUPPORTED) {
    return USB_BOT_READ_FAILED;
  }
  if (!count || !bot->block_bytes || lba >= bot->blocks || count > bot->blocks - lba ||
      count > scratch_bytes / bot->block_bytes || count > USB_BULK_BYTES / bot->block_bytes ||
      (!bot->wide && (count > UINT16_MAX || lba > UINT32_MAX ||
                     count - 1 > UINT32_MAX - lba))) {
    fail(bot, "private SCSI read range exceeds geometry or transfer bound");
    return USB_BOT_READ_FAILED;
  }
  uint8_t cdb[16] = {0};
  unsigned cdb_bytes;
  if (bot->wide) {
    cdb[0] = SCSI_READ_16;
    write_big(cdb + 2, lba, 8);
    write_big(cdb + 10, count, 4);
    cdb_bytes = 16;
  } else {
    cdb[0] = SCSI_READ_10;
    write_big(cdb + 2, lba, 4);
    write_big(cdb + 7, count, 2);
    cdb_bytes = 10;
  }
  size_t expected = (size_t)count * bot->block_bytes, bytes;
  enum bot_command_result result = command(bot, cdb, cdb_bytes, scratch, expected, &bytes, overall, submitted);
  if (result == BOT_COMMAND_REJECTED && request_sense(bot, scratch, overall)) {
    return USB_BOT_READ_CLEAN_REJECTED;
  }
  if (result != BOT_COMMAND_OK) {
    return bot->timed_out ? USB_BOT_READ_TIMED_OUT : USB_BOT_READ_FAILED;
  }
  if (bytes != expected) {
    fail(bot, "SCSI read did not return the complete requested blocks");
    return USB_BOT_READ_FAILED;
  }
  ++bot->reads;
  bot->read_bytes += bytes;
  return USB_BOT_READ_OK;
}

bool usb_bot_probe(struct usb_bot *bot, void *scratch, size_t scratch_bytes, uint64_t overall)
{
  if (!bot->host || !bot->configuration || !scratch || scratch_bytes < USB_BULK_BYTES ||
      bot->state == USB_BOT_FAILED || bot->state == USB_BOT_READY) {
    return false;
  }
  uint64_t deadline = bounded_deadline(overall, USB_BOT_TIMEOUT_MS);
  struct usb_setup setup = {
    .request_type = BOT_REQUEST_DEVICE_OUT, .request = BOT_REQUEST_SET_CONFIGURATION,
    .value = bot->configuration
  };
  size_t bytes;
  if (control(bot, &setup, NULL, 0, &bytes, deadline) != USB_OK) {
    return fail(bot, "BOT SET_CONFIGURATION failed");
  }
  if (bot->alternate) {
    setup = (struct usb_setup) {
      .request_type = BOT_REQUEST_INTERFACE_OUT, .request = BOT_REQUEST_SET_INTERFACE,
      .value = bot->alternate, .index = bot->interface
    };
    if (control(bot, &setup, NULL, 0, &bytes, deadline) != USB_OK) {
      return fail(bot, "BOT SET_INTERFACE failed");
    }
  }
  enum usb_result result = usb_host_configure_bulk(bot->host, &bot->in, &bot->out, deadline);
  if (result != USB_OK) {
    return result == USB_UNSUPPORTED ? unsupported(bot, "BOT bulk endpoint admission unavailable") :
                                       fail(bot, "BOT bulk endpoint configuration failed");
  }
  setup = (struct usb_setup) {
    .request_type = BOT_REQUEST_CLASS_INTERFACE_IN, .request = BOT_REQUEST_GET_MAX_LUN,
    .index = bot->interface, .length = 1
  };
  uint8_t max_lun = 0;
  result = control(bot, &setup, &max_lun, sizeof(max_lun), &bytes,
                   bounded_deadline(overall, USB_BOT_TIMEOUT_MS));
  if (result != USB_STALL && (result != USB_OK || bytes != 1 || max_lun > BOT_MAX_LUN)) {
    return fail(bot, "BOT GET_MAX_LUN returned invalid data or transport failure");
  }
  if (max_lun) {
    return unsupported(bot, "BOT multiple LUNs are deferred");
  }
  uint8_t cdb[6] = { SCSI_INQUIRY, 0, 0, 0, SCSI_INQUIRY_BYTES, 0 };
  if (!require_command(bot, cdb, sizeof(cdb), scratch, SCSI_INQUIRY_BYTES, &bytes, overall)) {
    return false;
  }
  const uint8_t *inquiry = scratch;
  if (bytes < SCSI_INQUIRY_BYTES || 5u + inquiry[4] < SCSI_INQUIRY_BYTES) {
    return fail(bot, "truncated standard SCSI INQUIRY response");
  }
  if ((inquiry[0] >> SCSI_QUALIFIER_SHIFT) ||
      (inquiry[0] & SCSI_DEVICE_TYPE_MASK) != SCSI_DEVICE_DIRECT_ACCESS) {
    return unsupported(bot, "BOT LUN 0 is not a connected direct-access SCSI device");
  }
  if (!ready(bot, scratch, overall) || !capacity(bot, scratch, overall)) {
    return false;
  }
  uint32_t count = USB_BULK_BYTES / bot->block_bytes;
  if (bot->blocks < count) {
    count = bot->blocks;
  }
  enum usb_bot_read_result read_result = usb_bot_read(bot, 0, count, scratch, scratch_bytes, overall, NULL);
  if (read_result != USB_BOT_READ_OK) {
    if (read_result == USB_BOT_READ_CLEAN_REJECTED) {
      fail(bot, "SCSI command rejected; sense retained");
    }
    return false;
  }
  memcpy(bot->first_sample, scratch, sizeof(bot->first_sample));
  read_result = usb_bot_read(bot, bot->blocks - 1, 1, scratch, scratch_bytes, overall, NULL);
  if (read_result != USB_BOT_READ_OK) {
    if (read_result == USB_BOT_READ_CLEAN_REJECTED) {
      fail(bot, "SCSI command rejected; sense retained");
    }
    return false;
  }
  memcpy(bot->last_sample, scratch, sizeof(bot->last_sample));
  bot->state = USB_BOT_READY;
  bot->detail = "read-only BOT first-span and final-block probe complete";
  return true;
}
