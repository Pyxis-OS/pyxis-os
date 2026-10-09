#ifndef BLUETOOTH_FIRMWARE_H
#define BLUETOOTH_FIRMWARE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <kernel/initrd.h>

#define BLUETOOTH_FIRMWARE_COMMAND_MAX 258

enum bluetooth_firmware_phase {
  BLUETOOTH_FIRMWARE_VERSION,
  BLUETOOTH_FIRMWARE_BOOT_PARAMETERS,
  BLUETOOTH_FIRMWARE_VALIDATE_SFI,
  BLUETOOTH_FIRMWARE_VALIDATE_DDC,
  BLUETOOTH_FIRMWARE_SECURE_INIT,
  BLUETOOTH_FIRMWARE_SECURE_KEY,
  BLUETOOTH_FIRMWARE_SECURE_SIGNATURE,
  BLUETOOTH_FIRMWARE_SECURE_DATA,
  BLUETOOTH_FIRMWARE_SECURE_RESULT,
  BLUETOOTH_FIRMWARE_BOOT,
  BLUETOOTH_FIRMWARE_VERSION_BOOTED,
  BLUETOOTH_FIRMWARE_RESET,
  BLUETOOTH_FIRMWARE_VERSION_AFTER,
  BLUETOOTH_FIRMWARE_DDC,
  BLUETOOTH_FIRMWARE_READY,
  BLUETOOTH_FIRMWARE_FAILED,
};

enum bluetooth_firmware_progress {
  BLUETOOTH_FIRMWARE_IDLE,
  BLUETOOTH_FIRMWARE_COMMAND,
  BLUETOOTH_FIRMWARE_DONE,
  BLUETOOTH_FIRMWARE_ERROR,
};

enum bluetooth_firmware_route {
  BLUETOOTH_FIRMWARE_CONTROL,
  BLUETOOTH_FIRMWARE_BULK,
};

enum bluetooth_firmware_completion {
  BLUETOOTH_FIRMWARE_COMMAND_COMPLETE,
  BLUETOOTH_FIRMWARE_BOOT_NOTIFICATION,
};

struct bluetooth_firmware_command {
  size_t length;
  uint64_t deadline;
  enum bluetooth_firmware_route route;
  enum bluetooth_firmware_completion completion;
};

struct bluetooth_firmware_boot_parameters {
  uint16_t device_revision;
  uint8_t otp_format, otp_content, otp_patch;
  uint8_t secure_boot, key_from_header, key_type, otp_lock, api_lock, debug_lock;
  uint8_t minimum_build, minimum_week, minimum_year, limited_cce, unlocked_state;
};

/* BSP-owned private initialization state. File views borrow the immutable
 * initrd for the kernel lifetime. Controller identity bytes are never retained.
 * USB tickets, command credits and endpoint framing remain the HCI owner's. */
struct bluetooth_firmware {
  enum bluetooth_firmware_phase phase, failure_phase;
  const char *failure;
  bool cold, bulk_events, pending, published, replied;
  bool secure_armed, secure_result, boot_armed, boot_notified;
  bool boot_parameters_found;
  uint8_t version[10];
  struct bluetooth_firmware_boot_parameters boot;
  struct initrd_file sfi, ddc;
  uint32_t boot_address;
  uint8_t image_build, image_week, image_year;
  size_t scan_offset, scan_group_bytes, offset, group_end, group_scan;
  size_t pending_bytes;
  size_t upload_commands, upload_bytes, ddc_commands;
  uint16_t pending_opcode;
  uint64_t command_deadline, upload_deadline, result_deadline, upload_started;
};

void bluetooth_firmware_init(struct bluetooth_firmware *state, uint64_t now);
enum bluetooth_firmware_progress bluetooth_firmware_prepare(struct bluetooth_firmware *state,
    uint8_t wire[BLUETOOTH_FIRMWARE_COMMAND_MAX], struct bluetooth_firmware_command *command,
    uint64_t now);
/* Call after successful nonblocking USB submission and ticket capture, before
 * collecting any later events. Preparing a command does not arm notifications. */
bool bluetooth_firmware_published(struct bluetooth_firmware *state, uint64_t now);
/* Validate the response, but keep the transaction until the owner also collects
 * its USB completion. BOOT has no Command Complete and must not call reply. */
bool bluetooth_firmware_reply(struct bluetooth_firmware *state, uint16_t opcode,
    const uint8_t *reply, size_t length, uint64_t now);
/* Caller confirms USB completion plus either the validated command response or
 * real boot notification. For BOOT, it also confirms no partial boot event at
 * the bulk framing boundary. A new operational command window starts there. */
bool bluetooth_firmware_retired(struct bluetooth_firmware *state, uint64_t now);
bool bluetooth_firmware_notify(struct bluetooth_firmware *state,
    const uint8_t *wire, size_t length, uint64_t now);
void bluetooth_firmware_tick(struct bluetooth_firmware *state, uint64_t now);
const char *bluetooth_firmware_phase_name(const struct bluetooth_firmware *state);
const char *bluetooth_firmware_reason(const struct bluetooth_firmware *state);

#endif
