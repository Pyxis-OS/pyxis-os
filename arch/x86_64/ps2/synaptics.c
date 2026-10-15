#include "synaptics.h"

#include <kernel/log.h>

/* Independent implementation of Synaptics PS/2 Interfacing Guide Rev. B,
 * sections 3.2, 4.2-4.4 and 5.1.5. Newer capability layouts were checked
 * against Linux v6.17; no Linux implementation is incorporated here. */
#define PS2_DISABLE_REPORTING 0xf5
#define PS2_SET_SCALING_1_1 0xe6
#define PS2_SET_RESOLUTION 0xe8
#define PS2_SET_SAMPLE_RATE 0xf3
#define PS2_GET_INFORMATION 0xe9
#define PS2_READ_DATA 0xeb
#define PS2_GET_DEVICE_ID 0xf2
#define SYNAPTICS_SET_MODE 0x14
#define SYNAPTICS_SET_AGM 0xc8
#define QUERY_IDENTITY 0x00
#define QUERY_MODES 0x01
#define QUERY_CAPABILITIES 0x02
#define QUERY_MODEL 0x03
#define QUERY_RESOLUTION 0x08
#define QUERY_EXTENDED 0x09
#define QUERY_FIRMWARE 0x0a
#define QUERY_CONTINUED 0x0c
#define QUERY_MAXIMUM 0x0d
#define QUERY_MINIMUM 0x0f
#define QUERY_MORE 0x10
#define ID_SIGNATURE_MASK UINT32_C(0x00ff00)
#define ID_SIGNATURE UINT32_C(0x004700)
#define MODEL_NEW_ABSOLUTE (1u << 7)
#define MODEL_ROTATED (1u << 23)
#define MODEL_PORTRAIT (1u << 22)
#define CAP_EXTENDED (1u << 23)
#define CAP_MIDDLE (1u << 18)
#define CAP_GUEST (1u << 7)
#define CAP_FOUR_BUTTONS (1u << 3)
#define CAP_MULTIFINGER (1u << 1)
#define CAP_WIDTH (1u << 0)
#define CAP_CLICKPAD_PRIMARY (1u << 20)
#define CAP_CLICKPAD_SECONDARY (1u << 8)
#define CAP_MAXIMUM (1u << 17)
#define CAP_MINIMUM (1u << 13)
#define CAP_IMAGE_SENSOR (1u << 11)
#define CAP_STICK_BUTTONS (1u << 16)
#define MODES_MORE_QUERIES (1u << 17)
#define RESOLUTION_VALID (1u << 15)
#define FIRMWARE_MODES_LAYOUT 0x705
#define FIRMWARE_8_1 0x801
#define FIRMWARE_8_2 0x802
#define MODE_ABSOLUTE (1u << 7)
#define MODE_HIGH_RATE (1u << 6)
#define MODE_DISABLE_GESTURES (1u << 2)
#define MODE_EXTENDED_WIDTH (1u << 2)
#define MODE_WIDTH (1u << 0)
#define PACKET_BYTES 6
#define PACKET_HEADER_MASK 0xc8
#define PACKET_FIRST_HEADER 0x80
#define PACKET_SECOND_HEADER 0xc0
#define PACKET_LEFT (1u << 0)
#define PACKET_RIGHT (1u << 1)
#define RELATIVE_ALWAYS_ONE (1u << 3)
#define RELATIVE_X_NEGATIVE (1u << 4)
#define RELATIVE_Y_NEGATIVE (1u << 5)
#define RELATIVE_OVERFLOW (3u << 6)
#define DEVICE_SELF_TEST 0xaa
#define DEVICE_RESET_ID 0x00
#define COORDINATE_LIMIT 8192
#define AGM_TYPE_MASK 0x30
#define AGM_POSITION 1
#define AGM_CONTACT_COUNT 2
#define DIAGNOSTIC_INTERVAL_NS UINT64_C(5000000000)
#define DIAGNOSTIC_DURATION_NS UINT64_C(150000000000)

struct synaptics_inventory {
  uint32_t identity, model, firmware, modes;
  uint32_t capabilities, extended, continued, more;
  uint32_t resolution, minimum, maximum;
  unsigned extended_buttons;
  bool geometry, calibrated_geometry, clickpad, stick_buttons;
};

static struct synaptics_inventory inventory;
static struct mouse_profile profile;
static bool diagnostics;
static uint8_t packet[PACKET_BYTES];
static unsigned packet_index;
static bool reset_prefix;
static unsigned guest_packet_buttons, stick_buttons;
static bool stick_buttons_observed;
static struct touchpad_sample second_contact;
static unsigned master_fingers, agm_fingers;
static bool second_since_master;
static volatile uint64_t malformed_headers, unsupported_packets, guest_overflows;
static volatile uint64_t reset_packets, decoded_touch_packets, decoded_guest_packets;
static volatile uint64_t decoded_agm_positions, decoded_agm_counts, unsupported_agm_packets;
static bool diagnostic_started, diagnostic_finished;
static uint64_t diagnostic_started_at, diagnostic_reported_at;

static void calibrate_geometry(void)
{
  /* Owner measured 102x70 mm three times. X45 implies 98.04 mm usable width
   * with 1.98 mm borders; queried Y54 implies 65.41 mm and 2.30 mm borders.
   * Those borders are authored allowances, not measured sensor dimensions.
   * Query 01's low byte is the current mode, not part of the fingerprint. */
  if (inventory.identity != UINT32_C(0x20471a) ||
      inventory.model != UINT32_C(0x01e2a1) ||
      inventory.firmware != UINT32_C(0x3c9cfd) ||
      (inventory.modes & UINT32_C(0xffff00)) != UINT32_C(0x378f00) ||
      inventory.capabilities != UINT32_C(0xf014a3) ||
      inventory.extended != UINT32_C(0x940300) ||
      inventory.continued != UINT32_C(0x12e800) ||
      inventory.more != UINT32_C(0x500000) ||
      inventory.resolution != UINT32_C(0x008036) ||
      inventory.minimum != UINT32_C(0x275924) ||
      inventory.maximum != UINT32_C(0xb1b792)) {
    return;
  }
  profile.x_resolution = 45;
  inventory.calibrated_geometry = true;
}

static bool encoded_argument(struct ps2_setup *setup, uint8_t argument)
{
  /* Delimit the four resolution commands even after a preceding query. */
  if (!mouse_ps2_command(setup, PS2_SET_SCALING_1_1)) {
    return false;
  }
  for (unsigned shift = 8; shift; shift -= 2) {
    if (!mouse_ps2_command(setup, PS2_SET_RESOLUTION) ||
        !mouse_ps2_command(setup, (argument >> (shift - 2)) & 3)) {
      return false;
    }
  }
  return true;
}

static bool read_bytes(struct ps2_setup *setup, uint8_t *bytes, unsigned count)
{
  for (unsigned i = 0; i < count; ++i) {
    if (!mouse_ps2_reply(setup, &bytes[i], MOUSE_PS2_REPLY_TIMEOUT_NS)) {
      return false;
    }
  }
  return true;
}

static bool query(struct ps2_setup *setup, uint8_t selector, uint32_t *value)
{
  uint8_t response[3];
  if (!encoded_argument(setup, selector) ||
      !mouse_ps2_command(setup, PS2_GET_INFORMATION) || !read_bytes(setup, response, 3)) {
    if (diagnostics) {
      kinfo("synaptics: query 0x%x failed\n", selector);
    }
    return false;
  }
  *value = (uint32_t)response[0] << 16 | (uint32_t)response[1] << 8 | response[2];
  if (diagnostics) {
    kinfo("synaptics: query 0x%x = %x %x %x\n", selector, response[0], response[1], response[2]);
  }
  return true;
}

static bool set_mode(struct ps2_setup *setup, uint8_t mode)
{
  uint32_t modes;
  if (!mouse_ps2_command(setup, PS2_DISABLE_REPORTING) ||
      !encoded_argument(setup, mode) || !mouse_ps2_command(setup, PS2_SET_SAMPLE_RATE) ||
      !mouse_ps2_command(setup, SYNAPTICS_SET_MODE) || !query(setup, QUERY_MODES, &modes)) {
    return false;
  }
  /* Query 01 retains the mode in its last byte across the 7.5 layout change. */
  return (modes & 0xff) == mode;
}

static bool enable_agm(struct ps2_setup *setup)
{
  return encoded_argument(setup, QUERY_MODEL) &&
         mouse_ps2_command(setup, PS2_SET_SAMPLE_RATE) &&
         mouse_ps2_command(setup, SYNAPTICS_SET_AGM);
}

static bool absolute_headers(const uint8_t *bytes)
{
  return (bytes[0] & PACKET_HEADER_MASK) == PACKET_FIRST_HEADER &&
         (bytes[3] & PACKET_HEADER_MASK) == PACKET_SECOND_HEADER;
}

static unsigned width_code(const uint8_t *bytes)
{
  return ((bytes[0] >> 4) & 3) * 4 + ((bytes[0] >> 2) & 1) * 2 +
         ((bytes[3] >> 2) & 1);
}

static unsigned touch_button_snapshot(const uint8_t *bytes);

static bool prove_format(struct ps2_setup *setup, bool absolute)
{
  uint8_t bytes[PACKET_BYTES];
  if (!mouse_ps2_command(setup, PS2_READ_DATA) ||
      !read_bytes(setup, bytes, absolute ? PACKET_BYTES : 3)) {
    return false;
  }
  if (absolute) {
    unsigned width = width_code(bytes);
    if (!absolute_headers(bytes) || width == 2 || width == 3) {
      return false;
    }
    profile.initial_touch_buttons = touch_button_snapshot(bytes);
    return true;
  }
  return bytes[0] & RELATIVE_ALWAYS_ONE;
}

static void decode_bound(uint32_t response, int32_t *x, int32_t *y)
{
  *x = ((response >> 16) & 0xff) * 32 + ((response >> 8) & 0x0f) * 2;
  *y = (response & 0xff) * 32 + ((response >> 12) & 0x0f) * 2;
}

static bool read_inventory(struct ps2_setup *setup)
{
  unsigned major = inventory.identity & 0x0f;
  unsigned version = major * 256 + (inventory.identity >> 16);
  if (!query(setup, QUERY_MODEL, &inventory.model) ||
      !query(setup, QUERY_CAPABILITIES, &inventory.capabilities) ||
      !query(setup, QUERY_MODES, &inventory.modes)) {
    return false;
  }
  if (version < FIRMWARE_MODES_LAYOUT &&
      (inventory.capabilities & ID_SIGNATURE_MASK) != ID_SIGNATURE) {
    return false;
  }
  unsigned extensions = (inventory.capabilities >> 20) & 7;
  if (!(inventory.capabilities & CAP_EXTENDED)) {
    extensions = 0;
  }
  if (extensions && !query(setup, QUERY_EXTENDED, &inventory.extended)) {
    return false;
  }
  if (extensions >= 2 && !query(setup, QUERY_FIRMWARE, &inventory.firmware)) {
    return false;
  }
  if (extensions >= 4 && !query(setup, QUERY_CONTINUED, &inventory.continued)) {
    return false;
  }
  if (version >= FIRMWARE_MODES_LAYOUT && (inventory.modes & MODES_MORE_QUERIES) &&
      !query(setup, QUERY_MORE, &inventory.more)) {
    return false;
  }
  inventory.extended_buttons = (inventory.extended >> 12) & 0x0f;
  inventory.clickpad = (inventory.continued & CAP_CLICKPAD_PRIMARY) &&
                      !(inventory.continued & CAP_CLICKPAD_SECONDARY);
  inventory.stick_buttons = (inventory.more & CAP_STICK_BUTTONS) &&
                            inventory.extended_buttons >= 3;
  if (major >= 4) {
    if (!query(setup, QUERY_RESOLUTION, &inventory.resolution)) {
      return false;
    }
    if (inventory.resolution & RESOLUTION_VALID) {
      profile.x_resolution = inventory.resolution >> 16;
      profile.y_resolution = inventory.resolution & 0xff;
    }
  }
  if (extensions >= 5 && (inventory.continued & CAP_MAXIMUM)) {
    if (!query(setup, QUERY_MAXIMUM, &inventory.maximum)) {
      return false;
    }
    decode_bound(inventory.maximum, &profile.x_max, &profile.y_max);
  }
  /* Firmware 8.1 undercounts query pages; its advertised minimum query is
   * independently documented by the pinned protocol reference. */
  bool minimum_supported = (inventory.continued & CAP_MINIMUM) &&
                           (extensions >= 7 || version == FIRMWARE_8_1);
  if (minimum_supported) {
    if (!query(setup, QUERY_MINIMUM, &inventory.minimum)) {
      return false;
    }
    decode_bound(inventory.minimum, &profile.x_min, &profile.y_min);
  }
  calibrate_geometry();
  inventory.geometry = minimum_supported &&
                       (inventory.continued & CAP_MAXIMUM) &&
                       profile.x_resolution && profile.y_resolution &&
                       profile.x_min < profile.x_max && profile.y_min < profile.y_max &&
                       profile.x_max < COORDINATE_LIMIT && profile.y_max < COORDINATE_LIMIT;
  return true;
}

static enum synaptics_setup_result recover_relative(struct ps2_setup *setup,
                                                    bool allow_legacy, const char *reason)
{
  synaptics_reset_decoder();
  profile.absolute = profile.has_guest = profile.has_second = false;
  bool tap_off = (inventory.identity & 0x0f) >= 4;
  setup->step = "Synaptics relative recovery";
  /* Reset/defaults clear Absolute but retain other mode bits. Explicit mode
   * programming removes W/EW/guest changes before accepting relative bytes. */
  uint8_t id;
  bool restored = mouse_ps2_reset(setup) && mouse_ps2_command(setup, PS2_GET_DEVICE_ID) &&
                  read_bytes(setup, &id, 1) && id == DEVICE_RESET_ID &&
                  set_mode(setup, tap_off ? MODE_DISABLE_GESTURES : 0) &&
                  prove_format(setup, false);
  if (diagnostics) {
    kinfo("synaptics: %s; relative=%u tap-off=%u legacy-allowed=%u\n", reason,
          restored, restored && tap_off, allow_legacy);
  }
  if (!restored || (!tap_off && !allow_legacy)) {
    return SYNAPTICS_UNAVAILABLE;
  }
  return SYNAPTICS_RELATIVE;
}

enum synaptics_setup_result synaptics_configure(struct ps2_setup *setup, bool activate,
                                               bool diagnose)
{
  diagnostics = diagnose;
  setup->step = "Synaptics identity";
  if (!mouse_ps2_command(setup, PS2_DISABLE_REPORTING) ||
      !query(setup, QUERY_IDENTITY, &inventory.identity)) {
    return mouse_ps2_reset(setup) ? SYNAPTICS_NOT_DEVICE : SYNAPTICS_UNAVAILABLE;
  }
  if ((inventory.identity & ID_SIGNATURE_MASK) != ID_SIGNATURE) {
    if (diagnostics) {
      kinfo("synaptics: identity signature absent; restore ordinary PS/2\n");
    }
    return mouse_ps2_reset(setup) ? SYNAPTICS_NOT_DEVICE : SYNAPTICS_UNAVAILABLE;
  }
  setup->step = "Synaptics inventory";
  if (!read_inventory(setup)) {
    /* No absolute mode was entered; unqualified pads retain legacy motion. */
    return recover_relative(setup, true, "inventory query failed");
  }
  if (diagnostics) {
    kinfo("synaptics: firmware=%u.%u newabs=%u W=%u clickpad=%u ext-buttons=%u pass-through=%u ext-stick-buttons=%u\n",
          inventory.identity & 0x0f, inventory.identity >> 16,
          !!(inventory.model & MODEL_NEW_ABSOLUTE), !!(inventory.capabilities & CAP_EXTENDED),
          inventory.clickpad, inventory.extended_buttons,
          !!(inventory.capabilities & CAP_GUEST), inventory.stick_buttons);
    kinfo("synaptics: bounds x=%d..%d y=%d..%d raw-resolution=%u,%u/mm effective-resolution=%u,%u/mm geometry=%u source=%s\n",
          profile.x_min, profile.x_max, profile.y_min, profile.y_max,
          inventory.resolution >> 16, inventory.resolution & 0xff,
          profile.x_resolution, profile.y_resolution, inventory.geometry,
          inventory.calibrated_geometry ? "owner-measured-T14-102x70" : "queried");
  }
  if (!activate || !inventory.calibrated_geometry) {
    /* Only the calibrated fingerprint has native absolute qualification. */
    if (diagnostics && activate) {
      kinfo("synaptics: no matching calibrated profile; restore ordinary PS/2\n");
    }
    return mouse_ps2_reset(setup) && set_mode(setup, 0) ?
           SYNAPTICS_NOT_DEVICE : SYNAPTICS_UNAVAILABLE;
  }
  if (!inventory.geometry) {
    return recover_relative(setup, true, "missing usable queried geometry");
  }
  if (!(inventory.model & MODEL_NEW_ABSOLUTE) || !(inventory.capabilities & CAP_EXTENDED) ||
      (inventory.model & (MODEL_ROTATED | MODEL_PORTRAIT))) {
    return recover_relative(setup, false, "unqualified absolute layout/orientation");
  }
  if ((inventory.capabilities & CAP_FOUR_BUTTONS) ||
      (inventory.extended_buttons &&
       (!inventory.stick_buttons || inventory.extended_buttons != 3))) {
    return recover_relative(setup, false, "unqualified physical-button layout");
  }
  setup->step = "Synaptics basic absolute W";
  uint8_t mode = MODE_ABSOLUTE | MODE_HIGH_RATE | MODE_WIDTH;
  bool image_sensor = inventory.continued & CAP_IMAGE_SENSOR;
  if (image_sensor) {
    /* With W enabled, bit 2 admits the secondary-position packet format. */
    mode |= MODE_EXTENDED_WIDTH;
  }
  if (!set_mode(setup, mode) || !prove_format(setup, true)) {
    return recover_relative(setup, false, "absolute mode/packet verification failed");
  }
  if (image_sensor) {
    setup->step = "Synaptics image-sensor AGM";
    if (!enable_agm(setup)) {
      return recover_relative(setup, false, "image-sensor AGM activation failed");
    }
    profile.has_second = true;
  }
  profile.absolute = true;
  profile.has_guest = (inventory.capabilities & CAP_GUEST) || inventory.stick_buttons;
  if (diagnostics) {
    kinfo("synaptics: qualified six-byte W, mode=0x%x, EW=%u, image-sensor AGM-requested=%u\n",
          mode, !!(mode & MODE_EXTENDED_WIDTH), profile.has_second);
  }
  return SYNAPTICS_ABSOLUTE;
}

const struct mouse_profile *synaptics_profile(void)
{
  return &profile;
}

void synaptics_diagnose(uint64_t now)
{
  if (!diagnostics || !profile.absolute || diagnostic_finished) {
    return;
  }
  if (!diagnostic_started) {
    diagnostic_started = true;
    diagnostic_started_at = diagnostic_reported_at = now;
  }
  if (now < diagnostic_started_at || now < diagnostic_reported_at ||
      now - diagnostic_reported_at < DIAGNOSTIC_INTERVAL_NS) {
    return;
  }
  kinfo("synaptics-transport: t=%lu s touch-packets=%lu guest-packets=%lu guest-overflows=%lu malformed-headers=%lu unsupported-packets=%lu reset-packets=%lu AGM-positions=%lu AGM-counts=%lu AGM-unsupported=%lu\n",
        (now - diagnostic_started_at) / UINT64_C(1000000000),
        decoded_touch_packets, decoded_guest_packets, guest_overflows,
        malformed_headers, unsupported_packets, reset_packets,
        decoded_agm_positions, decoded_agm_counts, unsupported_agm_packets);
  diagnostic_reported_at = now;
  diagnostic_finished = now - diagnostic_started_at >= DIAGNOSTIC_DURATION_NS;
}

void synaptics_reset_decoder(void)
{
  packet_index = 0;
  reset_prefix = false;
  guest_packet_buttons = stick_buttons = 0;
  stick_buttons_observed = false;
  second_contact = (struct touchpad_sample){0};
  master_fingers = agm_fingers = 0;
  second_since_master = false;
}

static unsigned physical_buttons(uint8_t bits)
{
  unsigned buttons = 0;
  if (bits & PACKET_LEFT) {
    buttons |= MOUSE_BUTTON_LEFT;
  }
  if (bits & PACKET_RIGHT) {
    buttons |= MOUSE_BUTTON_RIGHT;
  }
  if (bits & (1u << 2)) {
    buttons |= MOUSE_BUTTON_MIDDLE;
  }
  return buttons;
}

static unsigned touch_button_snapshot(const uint8_t *bytes)
{
  unsigned difference = bytes[0] ^ bytes[3];
  if (inventory.clickpad) {
    return difference & PACKET_LEFT ? MOUSE_BUTTON_LEFT : 0;
  }
  unsigned buttons = physical_buttons(bytes[0] & 3);
  if ((inventory.capabilities & CAP_MIDDLE) && (difference & PACKET_LEFT)) {
    buttons |= MOUSE_BUTTON_MIDDLE;
  }
  return buttons;
}

static void invalidate_second(void)
{
  second_contact = (struct touchpad_sample){0};
  second_since_master = false;
}

static void decode_agm(uint64_t observed_at)
{
  unsigned type = (packet[5] & AGM_TYPE_MASK) >> 4;
  if (type == AGM_POSITION) {
    /* Image-sensor AGM coordinates and pressure are half-resolution. */
    second_contact = (struct touchpad_sample){
      .x = ((packet[4] & 0x0f) * 256 + packet[1]) * 2,
      .y = ((packet[4] >> 4) * 256 + packet[2]) * 2,
      .pressure = ((packet[3] & 0x30) + (packet[5] & 0x0f)) * 2,
      .observed_at = observed_at,
      .valid = true,
    };
    second_since_master = true;
    ++decoded_agm_positions;
  } else if (type == AGM_CONTACT_COUNT) {
    if (master_fingers != packet[1] && !second_since_master) {
      invalidate_second();
    }
    agm_fingers = packet[1];
    ++decoded_agm_counts;
  } else {
    ++unsupported_agm_packets;
  }
}

static enum synaptics_decode_result decode_packet(uint64_t observed_at,
                                                 struct mouse_event *event)
{
  *event = (struct mouse_event){0};
  unsigned width = width_code(packet);
  /* W2 carries AGM data rather than a master/button snapshot. Its state is
   * attached to the next master; it never publishes a lift or switch change. */
  if (width == 2) {
    if (profile.has_second) {
      decode_agm(observed_at);
    } else {
      ++unsupported_packets;
      event->kind = MOUSE_EVENT_TOUCHPAD;
      return SYNAPTICS_EVENT;
    }
    return SYNAPTICS_INCOMPLETE;
  }
  event->touch_buttons = touch_button_snapshot(packet);
  event->touch_buttons_valid = true;
  if (width == 3) {
    /* Rev. B 5.1.5, Figure 5-3: with PackSize=0 the encapsulated fourth
     * guest byte is zero. This driver neither negotiates Guest4 nor sets it. */
    if (!(inventory.capabilities & CAP_GUEST) || !(packet[1] & RELATIVE_ALWAYS_ONE) ||
        packet[2] != 0) {
      ++unsupported_packets;
      *event = (struct mouse_event){.kind = MOUSE_EVENT_RESET};
      synaptics_reset_decoder();
      return SYNAPTICS_EVENT;
    }
    event->kind = MOUSE_EVENT_GUEST;
    guest_packet_buttons = physical_buttons(packet[1]);
    event->guest_buttons = guest_packet_buttons | stick_buttons;
    event->guest_buttons_valid = true;
    if (packet[1] & RELATIVE_OVERFLOW) {
      ++guest_overflows;
    } else {
      event->dx = (int32_t)packet[4] - ((packet[1] & RELATIVE_X_NEGATIVE) ? 256 : 0);
      event->dy = -((int32_t)packet[5] - ((packet[1] & RELATIVE_Y_NEGATIVE) ? 256 : 0));
    }
    ++decoded_guest_packets;
    return SYNAPTICS_EVENT;
  }

  event->kind = MOUSE_EVENT_TOUCHPAD;
  unsigned stolen_mask = 0;
  bool extended_present = (packet[0] ^ packet[3]) & PACKET_RIGHT;
  if (inventory.extended_buttons && extended_present) {
    unsigned bits = (inventory.extended_buttons + 1) / 2;
    stolen_mask = (1u << bits) - 1;
    if (inventory.stick_buttons) {
      unsigned buttons = (packet[4] & stolen_mask) | ((packet[5] & stolen_mask) << bits);
      stick_buttons = 0;
      stick_buttons_observed = true;
      if (buttons & 1) {
        stick_buttons |= MOUSE_BUTTON_LEFT;
      }
      if (buttons & 2) {
        stick_buttons |= MOUSE_BUTTON_MIDDLE;
      }
      if (buttons & 4) {
        stick_buttons |= MOUSE_BUTTON_RIGHT;
      }
      event->guest_buttons = guest_packet_buttons | stick_buttons;
      event->guest_buttons_valid = true;
    }
  } else if (inventory.stick_buttons && stick_buttons_observed &&
             !extended_present) {
    unsigned version = (inventory.identity & 0x0f) * 256 + (inventory.identity >> 16);
    /* In ordinary basic-W packets Ext=0 is a release. Firmware 8.1/8.2
     * instead omit this snapshot; guest packets always omit it. */
    if (version != FIRMWARE_8_1 && version != FIRMWARE_8_2) {
      stick_buttons = 0;
      event->guest_buttons = guest_packet_buttons;
      event->guest_buttons_valid = true;
    }
  }

  bool ambiguous = width <= 1 && !(inventory.capabilities & CAP_MULTIFINGER) &&
                   !profile.has_second;
  if (ambiguous) {
    ++unsupported_packets;
  }
  unsigned fingers = packet[2] && !ambiguous ?
                     (width == 0 ? 2 : width == 1 ? 3 : 1) : 0;
  if (profile.has_second && width == 1 && fingers && agm_fingers >= 3) {
    fingers = agm_fingers;
  }
  if (!fingers) {
    invalidate_second();
    agm_fingers = 0;
  } else if (fingers != master_fingers && !second_since_master) {
    invalidate_second();
  }
  /* W0 is the authoritative two-contact count; the AGM count refines W1. */
  if (width != 1) {
    agm_fingers = 0;
  }
  event->touch = (struct touchpad_sample){
    .x = ((packet[3] >> 4) & 1) * 4096 + (packet[1] & 0x0f) * 256 +
         (packet[4] & ~stolen_mask),
    .y = ((packet[3] >> 5) & 1) * 4096 + (packet[1] >> 4) * 256 +
         (packet[5] & ~stolen_mask),
    .pressure = packet[2],
    .width = width,
    .fingers = fingers,
    .observed_at = observed_at,
    .valid = true,
    .width_valid = width >= 4 && (inventory.capabilities & CAP_WIDTH),
  };
  if (fingers == 2 && second_contact.valid) {
    event->touch.second_x = second_contact.x;
    event->touch.second_y = second_contact.y;
    event->touch.second_pressure = second_contact.pressure;
    event->touch.second_at = second_contact.observed_at;
    event->touch.second_valid = true;
  }
  master_fingers = fingers;
  second_since_master = false;
  ++decoded_touch_packets;
  return SYNAPTICS_EVENT;
}

enum synaptics_decode_result synaptics_decode(uint8_t byte, uint64_t observed_at,
                                             struct mouse_event *event)
{
  if (!packet_index && reset_prefix && byte == DEVICE_RESET_ID) {
    ++reset_packets;
    synaptics_reset_decoder();
    *event = (struct mouse_event){.kind = MOUSE_EVENT_RESET};
    return SYNAPTICS_STOPPED;
  }
  reset_prefix = !packet_index && byte == DEVICE_SELF_TEST;
  if ((!packet_index && (byte & PACKET_HEADER_MASK) != PACKET_FIRST_HEADER) ||
      (packet_index == 3 && (byte & PACKET_HEADER_MASK) != PACKET_SECOND_HEADER)) {
    ++malformed_headers;
    packet_index = 0;
    guest_packet_buttons = stick_buttons = 0;
    stick_buttons_observed = false;
    invalidate_second();
    master_fingers = agm_fingers = 0;
    *event = (struct mouse_event){.kind = MOUSE_EVENT_RESET};
    return SYNAPTICS_EVENT;
  }
  packet[packet_index++] = byte;
  if (packet_index != PACKET_BYTES) {
    return SYNAPTICS_INCOMPLETE;
  }
  packet_index = 0;
  return decode_packet(observed_at, event);
}
