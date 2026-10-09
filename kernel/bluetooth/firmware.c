#include <kernel/log.h>
#include <kernel/memory.h>
#include "firmware.h"
#include "firmware-settings.h"

#define HCI_COMMAND_HEADER 3
#define HCI_OP_RESET 0x0c03
#define INTEL_OP_RESET 0xfc01
#define INTEL_OP_VERSION 0xfc05
#define INTEL_OP_SECURE_SEND 0xfc09
#define INTEL_OP_BOOT_PARAMETERS 0xfc0d
#define INTEL_OP_WRITE_BOOT_PARAMETERS 0xfc0e
#define INTEL_OP_DDC 0xfc8b
#define INTEL_EVENT_VENDOR 0xff
#define INTEL_EVENT_BOOT 0x02
#define INTEL_EVENT_SECURE_RESULT 0x06
#define INTEL_PLATFORM 0x37
#define INTEL_AX200_VARIANT 0x14
#define INTEL_AX200_HW_REVISION 1
#define INTEL_AX200_FW_REVISION 3
#define INTEL_AX200_DEVICE_REVISION 1
#define INTEL_BOOTLOADER 0x06
#define INTEL_OPERATIONAL 0x23
#define INTEL_BOOT_PARAMETERS_BYTES 23
#define INTEL_DDC_REPLY_BYTES 3
#define INTEL_SOFT_RESET 0
#define INTEL_PATCH_ENABLE 1
#define INTEL_DDC_RELOAD_DISABLED 0
#define INTEL_BOOT_FROM_ADDRESS 1
#define SFI_RSA_HEADER 644
#define SFI_CSS_VERSION_OFFSET 8
#define SFI_CSS_RSA_VERSION UINT32_C(0x00010000)
#define SFI_INIT_BYTES 128
#define SFI_KEY_OFFSET 128
#define SFI_SIGNATURE_OFFSET 388
#define SFI_KEY_BYTES 256
#define SFI_SIGNATURE_BYTES 256
#define SECURE_FRAGMENT_MAX 252
#define SECURE_TYPE_INIT 0
#define SECURE_TYPE_DATA 1
#define SECURE_TYPE_SIGNATURE 2
#define SECURE_TYPE_KEY 3
#define DDC_ID_BYTES 2

static const char sfi_path[] = "share/firmware/intel/ibt-20-1-3.sfi";
static const char ddc_path[] = "share/firmware/intel/ibt-20-1-3.ddc";
static const uint8_t development_version[] = {0, 0x37, 0x14, 1, 0x23, 3, 193, 33, 24, 0};

static uint16_t read16(const uint8_t *wire)
{
  return wire[0] | ((uint16_t)wire[1] << 8);
}

static uint32_t read32(const uint8_t *wire)
{
  return read16(wire) | ((uint32_t)read16(wire + 2) << 16);
}

static bool fail(struct bluetooth_firmware *state, const char *reason)
{
  if (state->phase != BLUETOOTH_FIRMWARE_FAILED) {
    state->failure = reason;
    state->failure_phase = state->phase;
    state->phase = BLUETOOTH_FIRMWARE_FAILED;
  }
  return false;
}

static bool deadline(struct bluetooth_firmware *state, uint64_t now,
    uint64_t duration, uint64_t *result)
{
  if (now > UINT64_MAX - duration) {
    return fail(state, "deadline overflow");
  }
  *result = now + duration;
  return true;
}

static bool hardware_profile(const uint8_t *version)
{
  return version[1] == INTEL_PLATFORM && version[2] == INTEL_AX200_VARIANT &&
      version[3] == INTEL_AX200_HW_REVISION && version[5] == INTEL_AX200_FW_REVISION;
}

static bool acquire_ddc(struct bluetooth_firmware *state)
{
  if (initrd_lookup(ddc_path, &state->ddc) != INITRD_OK || !state->ddc.size) {
    return fail(state, "DDC asset unavailable");
  }
  return true;
}

static bool acquire_sfi(struct bluetooth_firmware *state)
{
  if (initrd_lookup(sfi_path, &state->sfi) != INITRD_OK) {
    return fail(state, "SFI asset unavailable");
  }
  const uint8_t *bytes = state->sfi.data;
  if (state->sfi.size <= SFI_RSA_HEADER ||
      read32(bytes + SFI_CSS_VERSION_OFFSET) != SFI_CSS_RSA_VERSION) {
    return fail(state, "invalid RSA SFI header");
  }
  state->scan_offset = SFI_RSA_HEADER;
  return true;
}

static bool sfi_command(const struct bluetooth_firmware *state, size_t offset,
    size_t *length)
{
  if (offset > state->sfi.size || state->sfi.size - offset < HCI_COMMAND_HEADER) {
    return false;
  }
  const uint8_t *bytes = state->sfi.data;
  *length = HCI_COMMAND_HEADER + (size_t)bytes[offset + 2];
  return *length <= state->sfi.size - offset;
}

static bool image_meets_minimum(const struct bluetooth_firmware *state)
{
  if (state->image_year != state->boot.minimum_year) {
    return state->image_year > state->boot.minimum_year;
  }
  if (state->image_week != state->boot.minimum_week) {
    return state->image_week > state->boot.minimum_week;
  }
  return state->image_build >= state->boot.minimum_build;
}

static void validate_sfi(struct bluetooth_firmware *state)
{
  const uint8_t *bytes = state->sfi.data;
  for (unsigned i = 0; i < BLUETOOTH_FIRMWARE_SCAN_BUDGET &&
       state->scan_offset < state->sfi.size; ++i) {
    size_t length;
    if (!sfi_command(state, state->scan_offset, &length)) {
      fail(state, "truncated SFI command");
      return;
    }
    const uint8_t *command = bytes + state->scan_offset;
    if (read16(command) == INTEL_OP_WRITE_BOOT_PARAMETERS) {
      if (length != HCI_COMMAND_HEADER + 7 || state->boot_parameters_found) {
        fail(state, "invalid SFI boot parameters");
        return;
      }
      state->boot_parameters_found = true;
      state->boot_address = read32(command + HCI_COMMAND_HEADER);
      state->image_build = command[7];
      state->image_week = command[8];
      state->image_year = command[9];
    }
    state->scan_offset += length;
    state->scan_group_bytes += length;
    if (!(state->scan_group_bytes % 4)) {
      state->scan_group_bytes = 0;
    }
  }
  if (state->scan_offset != state->sfi.size) {
    return;
  }
  if (state->scan_group_bytes || !state->boot_parameters_found) {
    fail(state, "unaligned SFI payload or missing boot parameters");
    return;
  }
  if (!image_meets_minimum(state)) {
    fail(state, "SFI build below controller minimum");
    return;
  }
  ktrace("Bluetooth firmware: validated SFI bytes %zu, build %u/%u/%u\n",
      state->sfi.size, state->image_build, state->image_week, 2000 + state->image_year);
  state->scan_offset = 0;
  state->phase = BLUETOOTH_FIRMWARE_VALIDATE_DDC;
}

static bool ddc_record(const struct bluetooth_firmware *state, size_t offset,
    size_t *length)
{
  if (offset >= state->ddc.size) {
    return false;
  }
  const uint8_t *bytes = state->ddc.data;
  *length = 1 + (size_t)bytes[offset];
  return bytes[offset] >= DDC_ID_BYTES && *length <= UINT8_MAX &&
      *length <= state->ddc.size - offset;
}

static void validate_ddc(struct bluetooth_firmware *state)
{
  for (unsigned i = 0; i < BLUETOOTH_FIRMWARE_SCAN_BUDGET &&
       state->scan_offset < state->ddc.size; ++i) {
    size_t length;
    if (!ddc_record(state, state->scan_offset, &length)) {
      fail(state, "invalid DDC record");
      return;
    }
    state->scan_offset += length;
  }
  if (state->scan_offset == state->ddc.size) {
    state->offset = 0;
    state->phase = state->cold ? BLUETOOTH_FIRMWARE_SECURE_INIT : BLUETOOTH_FIRMWARE_RESET;
  }
}

void bluetooth_firmware_init(struct bluetooth_firmware *state, uint64_t now)
{
  (void)now;
  memset(state, 0, sizeof(*state));
  state->phase = BLUETOOTH_FIRMWARE_VERSION;
}

void bluetooth_firmware_tick(struct bluetooth_firmware *state, uint64_t now)
{
  if (state->phase == BLUETOOTH_FIRMWARE_FAILED || state->phase == BLUETOOTH_FIRMWARE_READY) {
    return;
  }
  if (state->pending && now >= state->command_deadline) {
    fail(state, state->phase == BLUETOOTH_FIRMWARE_BOOT ?
        "boot transaction timed out" : "firmware command timed out");
  } else if (state->upload_deadline && now >= state->upload_deadline) {
    fail(state, "firmware upload timed out");
  } else if (state->result_deadline && now >= state->result_deadline) {
    fail(state, "secure result timed out");
  }
}

static size_t secure_fragment(struct bluetooth_firmware *state, uint8_t *wire,
    uint8_t type, size_t base, size_t remaining)
{
  size_t length = remaining > SECURE_FRAGMENT_MAX ? SECURE_FRAGMENT_MAX : remaining;
  const uint8_t *bytes = state->sfi.data;
  wire[HCI_COMMAND_HEADER] = type;
  memcpy(wire + HCI_COMMAND_HEADER + 1, bytes + base + state->offset, length);
  state->pending_bytes = length;
  return length + 1;
}

static bool find_data_group(struct bluetooth_firmware *state)
{
  if (state->group_end > state->offset) {
    return true;
  }
  for (unsigned i = 0; i < BLUETOOTH_FIRMWARE_SCAN_BUDGET; ++i) {
    size_t length;
    if (!sfi_command(state, state->group_scan, &length)) {
      return fail(state, "invalid SFI data group");
    }
    state->group_scan += length;
    if (!((state->group_scan - state->offset) % 4)) {
      state->group_end = state->group_scan;
      return true;
    }
  }
  return false;
}

enum bluetooth_firmware_progress bluetooth_firmware_prepare(struct bluetooth_firmware *state,
    uint8_t wire[BLUETOOTH_FIRMWARE_COMMAND_MAX], struct bluetooth_firmware_command *command,
    uint64_t now)
{
  bluetooth_firmware_tick(state, now);
  if (state->phase == BLUETOOTH_FIRMWARE_FAILED) {
    return BLUETOOTH_FIRMWARE_ERROR;
  }
  if (state->phase == BLUETOOTH_FIRMWARE_READY) {
    return BLUETOOTH_FIRMWARE_DONE;
  }
  if (state->pending) {
    return BLUETOOTH_FIRMWARE_IDLE;
  }
  if (state->phase == BLUETOOTH_FIRMWARE_VALIDATE_SFI) {
    validate_sfi(state);
    return state->phase == BLUETOOTH_FIRMWARE_FAILED ?
        BLUETOOTH_FIRMWARE_ERROR : BLUETOOTH_FIRMWARE_IDLE;
  }
  if (state->phase == BLUETOOTH_FIRMWARE_VALIDATE_DDC) {
    validate_ddc(state);
    return state->phase == BLUETOOTH_FIRMWARE_FAILED ?
        BLUETOOTH_FIRMWARE_ERROR : BLUETOOTH_FIRMWARE_IDLE;
  }
  if (state->phase == BLUETOOTH_FIRMWARE_SECURE_RESULT) {
    if (!state->secure_result) {
      return BLUETOOTH_FIRMWARE_IDLE;
    }
    ktrace("Bluetooth firmware: upload complete, commands %zu, payload bytes %zu, duration ns %llu\n",
        state->upload_commands, state->upload_bytes,
        (unsigned long long)(now - state->upload_started));
    state->secure_armed = false;
    state->upload_deadline = state->result_deadline = 0;
    state->phase = BLUETOOTH_FIRMWARE_BOOT;
  }
  uint16_t opcode;
  size_t parameters = 0;
  *command = (struct bluetooth_firmware_command){
    .route = BLUETOOTH_FIRMWARE_CONTROL,
    .completion = BLUETOOTH_FIRMWARE_COMMAND_COMPLETE,
  };
  state->pending_bytes = 0;
  switch (state->phase) {
  case BLUETOOTH_FIRMWARE_BOOT_PARAMETERS:
    opcode = INTEL_OP_BOOT_PARAMETERS;
    break;
  case BLUETOOTH_FIRMWARE_SECURE_INIT:
    opcode = INTEL_OP_SECURE_SEND;
    parameters = secure_fragment(state, wire, SECURE_TYPE_INIT, 0, SFI_INIT_BYTES);
    break;
  case BLUETOOTH_FIRMWARE_SECURE_KEY:
    opcode = INTEL_OP_SECURE_SEND;
    parameters = secure_fragment(state, wire, SECURE_TYPE_KEY,
        SFI_KEY_OFFSET, SFI_KEY_BYTES - state->offset);
    break;
  case BLUETOOTH_FIRMWARE_SECURE_SIGNATURE:
    opcode = INTEL_OP_SECURE_SEND;
    parameters = secure_fragment(state, wire, SECURE_TYPE_SIGNATURE,
        SFI_SIGNATURE_OFFSET, SFI_SIGNATURE_BYTES - state->offset);
    break;
  case BLUETOOTH_FIRMWARE_SECURE_DATA:
    if (!find_data_group(state)) {
      return state->phase == BLUETOOTH_FIRMWARE_FAILED ?
          BLUETOOTH_FIRMWARE_ERROR : BLUETOOTH_FIRMWARE_IDLE;
    }
    opcode = INTEL_OP_SECURE_SEND;
    parameters = secure_fragment(state, wire, SECURE_TYPE_DATA, 0,
        state->group_end - state->offset);
    break;
  case BLUETOOTH_FIRMWARE_BOOT:
    opcode = INTEL_OP_RESET;
    parameters = 8;
    wire[3] = INTEL_SOFT_RESET;
    wire[4] = INTEL_PATCH_ENABLE;
    wire[5] = INTEL_DDC_RELOAD_DISABLED;
    wire[6] = INTEL_BOOT_FROM_ADDRESS;
    for (unsigned i = 0; i < 4; ++i) {
      wire[7 + i] = state->boot_address >> (i * 8);
    }
    command->completion = BLUETOOTH_FIRMWARE_BOOT_NOTIFICATION;
    break;
  case BLUETOOTH_FIRMWARE_RESET:
    opcode = HCI_OP_RESET;
    break;
  case BLUETOOTH_FIRMWARE_VERSION:
  case BLUETOOTH_FIRMWARE_VERSION_BOOTED:
  case BLUETOOTH_FIRMWARE_VERSION_AFTER:
    /* AX200 operational firmware also supports a TLV selector. This profile
     * uses the legacy zero-parameter query in both controller states. */
    opcode = INTEL_OP_VERSION;
    break;
  case BLUETOOTH_FIRMWARE_DDC:
    if (!ddc_record(state, state->offset, &parameters)) {
      fail(state, "invalid DDC cursor");
      return BLUETOOTH_FIRMWARE_ERROR;
    }
    opcode = INTEL_OP_DDC;
    memcpy(wire + HCI_COMMAND_HEADER, (const uint8_t *)state->ddc.data + state->offset,
        parameters);
    state->pending_bytes = parameters;
    break;
  default:
    fail(state, "invalid firmware phase");
    return BLUETOOTH_FIRMWARE_ERROR;
  }
  uint64_t duration = state->phase == BLUETOOTH_FIRMWARE_BOOT ?
      BLUETOOTH_FIRMWARE_BOOT_NS : BLUETOOTH_FIRMWARE_COMMAND_NS;
  if (!deadline(state, now, duration, &state->command_deadline)) {
    return BLUETOOTH_FIRMWARE_ERROR;
  }
  if (opcode == INTEL_OP_SECURE_SEND) {
    command->route = BLUETOOTH_FIRMWARE_BULK;
  }
  wire[0] = opcode;
  wire[1] = opcode >> 8;
  wire[2] = parameters;
  command->length = HCI_COMMAND_HEADER + parameters;
  command->deadline = state->command_deadline;
  state->pending_opcode = opcode;
  state->pending = true;
  state->replied = false;
  return BLUETOOTH_FIRMWARE_COMMAND;
}

bool bluetooth_firmware_published(struct bluetooth_firmware *state, uint64_t now)
{
  bluetooth_firmware_tick(state, now);
  if (state->phase == BLUETOOTH_FIRMWARE_FAILED) {
    return false;
  }
  if (!state->pending || state->published) {
    return fail(state, "unexpected firmware command publication");
  }
  state->published = true;
  if (state->pending_opcode == INTEL_OP_SECURE_SEND && !state->secure_armed) {
    state->secure_armed = true;
    state->upload_started = now;
  }
  if (state->phase == BLUETOOTH_FIRMWARE_BOOT) {
    state->boot_armed = true;
  }
  return true;
}

bool bluetooth_firmware_reply(struct bluetooth_firmware *state, uint16_t opcode,
    const uint8_t *reply, size_t length, uint64_t now)
{
  bluetooth_firmware_tick(state, now);
  if (state->phase == BLUETOOTH_FIRMWARE_FAILED) {
    return false;
  }
  if (!state->pending || !state->published || state->replied || opcode != state->pending_opcode ||
      state->phase == BLUETOOTH_FIRMWARE_BOOT) {
    return fail(state, "unexpected firmware command response");
  }
  if (!length || reply[0]) {
    return fail(state, "firmware command rejected");
  }
  if (opcode == INTEL_OP_VERSION) {
    if (length != sizeof(state->version)) {
      return fail(state, "invalid Intel version length");
    }
    ktrace("Bluetooth firmware: %s, platform %u variant %u hardware revision %u, "
        "firmware type %u revision %u build %u/%u/%u patch %u\n",
        bluetooth_firmware_phase_name(state), reply[1], reply[2], reply[3], reply[4],
        reply[5], reply[6], reply[7], 2000 + reply[8], reply[9]);
    if (!hardware_profile(reply)) {
      return fail(state, "unsupported Intel controller version");
    }
    if (state->phase == BLUETOOTH_FIRMWARE_VERSION) {
      if (reply[4] == INTEL_BOOTLOADER) {
        state->cold = state->bulk_events = true;
      } else if (memcmp(reply, development_version, sizeof(development_version))) {
        return fail(state, "running firmware outside development profile");
      }
      memcpy(state->version, reply, sizeof(state->version));
    } else if (state->phase == BLUETOOTH_FIRMWARE_VERSION_BOOTED) {
      if (reply[4] != INTEL_OPERATIONAL || reply[6] != state->image_build ||
          reply[7] != state->image_week || reply[8] != state->image_year) {
        return fail(state, "booted firmware does not match SFI metadata");
      }
      memcpy(state->version, reply, sizeof(state->version));
    } else if (memcmp(reply, state->version, sizeof(state->version))) {
      return fail(state, "firmware version changed after reset");
    }
  } else if (opcode == INTEL_OP_BOOT_PARAMETERS) {
    if (length != INTEL_BOOT_PARAMETERS_BYTES) {
      return fail(state, "invalid Intel boot parameters length");
    }
    state->boot = (struct bluetooth_firmware_boot_parameters){
      .device_revision = read16(reply + 4),
      .otp_format = reply[1], .otp_content = reply[2], .otp_patch = reply[3],
      .secure_boot = reply[6], .key_from_header = reply[7], .key_type = reply[8],
      .otp_lock = reply[9], .api_lock = reply[10], .debug_lock = reply[11],
      .minimum_build = reply[18], .minimum_week = reply[19], .minimum_year = reply[20],
      .limited_cce = reply[21], .unlocked_state = reply[22],
    };
    ktrace("Bluetooth firmware: boot device revision %u, OTP format/content/patch %u/%u/%u, "
        "secure boot %u key header/type %u/%u, locks OTP/API/debug %u/%u/%u, "
        "minimum build %u/%u/%u, limited CCE %u unlocked %u\n",
        state->boot.device_revision, state->boot.otp_format, state->boot.otp_content,
        state->boot.otp_patch, state->boot.secure_boot, state->boot.key_from_header,
        state->boot.key_type, state->boot.otp_lock, state->boot.api_lock,
        state->boot.debug_lock, state->boot.minimum_build, state->boot.minimum_week,
        2000 + state->boot.minimum_year, state->boot.limited_cce, state->boot.unlocked_state);
    if (state->boot.device_revision != INTEL_AX200_DEVICE_REVISION || state->boot.limited_cce) {
      return fail(state, "unsupported Intel boot parameters");
    }
  } else if (opcode == INTEL_OP_DDC) {
    if (length != INTEL_DDC_REPLY_BYTES) {
      return fail(state, "invalid DDC response length");
    }
    ktrace("Bluetooth firmware: DDC response identifier %u\n", read16(reply + 1));
  } else if (length != 1) {
    return fail(state, "invalid firmware command response length");
  }
  state->replied = true;
  return true;
}

bool bluetooth_firmware_retired(struct bluetooth_firmware *state, uint64_t now)
{
  bluetooth_firmware_tick(state, now);
  if (state->phase == BLUETOOTH_FIRMWARE_FAILED) {
    return false;
  }
  if (!state->pending || !state->published || (state->phase == BLUETOOTH_FIRMWARE_BOOT ?
      !state->boot_notified : !state->replied)) {
    return fail(state, "unconfirmed firmware command retirement");
  }
  if (state->pending_opcode == INTEL_OP_SECURE_SEND) {
    ++state->upload_commands;
    state->upload_bytes += state->pending_bytes;
  }
  state->pending = state->published = state->replied = false;
  state->command_deadline = 0;
  switch (state->phase) {
  case BLUETOOTH_FIRMWARE_VERSION:
    if (state->cold) {
      state->phase = BLUETOOTH_FIRMWARE_BOOT_PARAMETERS;
    } else {
      if (!acquire_ddc(state)) {
        return false;
      }
      state->phase = BLUETOOTH_FIRMWARE_VALIDATE_DDC;
    }
    break;
  case BLUETOOTH_FIRMWARE_BOOT_PARAMETERS:
    if (!acquire_ddc(state) || !acquire_sfi(state) ||
        !deadline(state, now, BLUETOOTH_FIRMWARE_UPLOAD_NS, &state->upload_deadline)) {
      return false;
    }
    state->phase = BLUETOOTH_FIRMWARE_VALIDATE_SFI;
    break;
  case BLUETOOTH_FIRMWARE_SECURE_INIT:
    state->offset = 0;
    state->phase = BLUETOOTH_FIRMWARE_SECURE_KEY;
    break;
  case BLUETOOTH_FIRMWARE_SECURE_KEY:
    state->offset += state->pending_bytes;
    if (state->offset == SFI_KEY_BYTES) {
      state->offset = 0;
      state->phase = BLUETOOTH_FIRMWARE_SECURE_SIGNATURE;
    }
    break;
  case BLUETOOTH_FIRMWARE_SECURE_SIGNATURE:
    state->offset += state->pending_bytes;
    if (state->offset == SFI_SIGNATURE_BYTES) {
      state->offset = state->group_scan = SFI_RSA_HEADER;
      state->group_end = SFI_RSA_HEADER;
      state->phase = BLUETOOTH_FIRMWARE_SECURE_DATA;
    }
    break;
  case BLUETOOTH_FIRMWARE_SECURE_DATA:
    state->offset += state->pending_bytes;
    if (state->offset == state->sfi.size) {
      state->phase = BLUETOOTH_FIRMWARE_SECURE_RESULT;
      if (!deadline(state, now, BLUETOOTH_FIRMWARE_RESULT_NS, &state->result_deadline)) {
        return false;
      }
    }
    break;
  case BLUETOOTH_FIRMWARE_BOOT:
    state->boot_armed = false;
    state->bulk_events = false;
    state->phase = BLUETOOTH_FIRMWARE_VERSION_BOOTED;
    break;
  case BLUETOOTH_FIRMWARE_VERSION_BOOTED:
    state->phase = BLUETOOTH_FIRMWARE_RESET;
    break;
  case BLUETOOTH_FIRMWARE_RESET:
    state->phase = BLUETOOTH_FIRMWARE_VERSION_AFTER;
    break;
  case BLUETOOTH_FIRMWARE_VERSION_AFTER:
    state->offset = 0;
    state->phase = BLUETOOTH_FIRMWARE_DDC;
    break;
  case BLUETOOTH_FIRMWARE_DDC:
    ++state->ddc_commands;
    state->offset += state->pending_bytes;
    if (state->offset == state->ddc.size) {
      ktrace("Bluetooth firmware: DDC complete, commands %zu, bytes %zu\n",
          state->ddc_commands, state->ddc.size);
      state->phase = BLUETOOTH_FIRMWARE_READY;
    }
    break;
  default:
    return fail(state, "invalid firmware retirement phase");
  }
  state->pending_bytes = 0;
  return true;
}

bool bluetooth_firmware_notify(struct bluetooth_firmware *state,
    const uint8_t *wire, size_t length, uint64_t now)
{
  bluetooth_firmware_tick(state, now);
  if (state->phase == BLUETOOTH_FIRMWARE_FAILED) {
    return false;
  }
  if (length < 2 || length != 2 + (size_t)wire[1]) {
    return fail(state, "invalid vendor event framing");
  }
  if (wire[0] != INTEL_EVENT_VENDOR || length < 3) {
    return true;
  }
  if (wire[2] == INTEL_EVENT_SECURE_RESULT) {
    if (!state->secure_armed || length != 7) {
      return fail(state, "unexpected secure result notification");
    }
    ktrace("Bluetooth firmware: secure notification result %u opcode %x status %u\n",
        wire[3], read16(wire + 4), wire[6]);
    if (wire[3]) {
      return fail(state, "secure firmware verification failed");
    }
    /* Endpoints complete independently: the final fragment can be acknowledged
     * before its USB ticket is collected, but it must already be published. */
    bool final_data = state->phase == BLUETOOTH_FIRMWARE_SECURE_DATA &&
        state->pending && state->published &&
        state->offset + state->pending_bytes == state->sfi.size;
    if (state->secure_result || (!final_data && state->phase != BLUETOOTH_FIRMWARE_SECURE_RESULT)) {
      return fail(state, "premature or duplicate secure result notification");
    }
    state->secure_result = true;
  } else if (wire[2] == INTEL_EVENT_BOOT) {
    if (!state->pending || !state->published || !state->boot_armed ||
        state->phase != BLUETOOTH_FIRMWARE_BOOT ||
        state->boot_notified || length != 9) {
      return fail(state, "unexpected boot notification");
    }
    ktrace("Bluetooth firmware: boot notification zero %u commands %u source %u "
        "reset type/reason %u/%u DDC status %u\n",
        wire[3], wire[4], wire[5], wire[6], wire[7], wire[8]);
    /* Field meanings beyond this event's framing are not qualified here.
     * In particular, num_cmds is not used to invent command allowance. */
    state->boot_notified = true;
  }
  return true;
}

const char *bluetooth_firmware_phase_name(const struct bluetooth_firmware *state)
{
  enum bluetooth_firmware_phase phase = state->phase == BLUETOOTH_FIRMWARE_FAILED ?
      state->failure_phase : state->phase;
  switch (phase) {
  case BLUETOOTH_FIRMWARE_VERSION: return "read version";
  case BLUETOOTH_FIRMWARE_BOOT_PARAMETERS: return "read boot parameters";
  case BLUETOOTH_FIRMWARE_VALIDATE_SFI: return "validate SFI";
  case BLUETOOTH_FIRMWARE_VALIDATE_DDC: return "validate DDC";
  case BLUETOOTH_FIRMWARE_SECURE_INIT: return "secure header";
  case BLUETOOTH_FIRMWARE_SECURE_KEY: return "secure key";
  case BLUETOOTH_FIRMWARE_SECURE_SIGNATURE: return "secure signature";
  case BLUETOOTH_FIRMWARE_SECURE_DATA: return "secure data";
  case BLUETOOTH_FIRMWARE_SECURE_RESULT: return "secure result";
  case BLUETOOTH_FIRMWARE_BOOT: return "boot firmware";
  case BLUETOOTH_FIRMWARE_VERSION_BOOTED: return "verify booted version";
  case BLUETOOTH_FIRMWARE_RESET: return "reset controller";
  case BLUETOOTH_FIRMWARE_VERSION_AFTER: return "verify reset version";
  case BLUETOOTH_FIRMWARE_DDC: return "apply DDC";
  case BLUETOOTH_FIRMWARE_READY: return "firmware ready";
  case BLUETOOTH_FIRMWARE_FAILED: return "firmware failed";
  }
  return "invalid firmware phase";
}

const char *bluetooth_firmware_reason(const struct bluetooth_firmware *state)
{
  return state->failure ? state->failure : "none";
}
