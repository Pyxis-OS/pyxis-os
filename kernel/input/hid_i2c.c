/* SPDX-License-Identifier: MPL-2.0 */
#include <kernel/input/hid_i2c.h>
#include <kernel/log.h>
#include <arch/clock.h>
#include <arch/cpu.h>
#include <kernel/input/hid_layout.h>
#include <kernel/input/hid_pointer.h>
#include <kernel/input/hid_i2c_metrics.h>
#include <kernel/mm/heap.h>
#include <kernel/memory.h>
#include <kernel/task.h>

#define HID_I2C_DESCRIPTOR_BYTES 30
#define HID_I2C_VERSION_1_00 0x0100
#define HID_I2C_READ_TIMEOUT_MS 100

static uint16_t read16(const uint8_t *bytes)
{
  return (uint16_t)bytes[0] | (uint16_t)bytes[1] << 8;
}

bool hid_i2c_descriptor_read(struct dw_i2c_master *master,
    const struct acpi_hid_i2c_endpoint *endpoint, struct hid_i2c_descriptor *descriptor)
{
  uint8_t address[] = {endpoint->descriptor_register, endpoint->descriptor_register >> 8};
  uint8_t bytes[HID_I2C_DESCRIPTOR_BYTES];
  enum dw_i2c_result result = dw_i2c_transfer(master, endpoint->address,
      endpoint->addressing_mode != 0, address, sizeof(address), bytes, sizeof(bytes),
      task_deadline_after_ms(HID_I2C_READ_TIMEOUT_MS));
  if (result != DW_I2C_OK) {
    kinfo("HID-I2C: descriptor read refused: %s address=0x%x register=0x%x "
        "abort=0x%x status=0x%x\n", dw_i2c_result_name(result), endpoint->address,
        endpoint->descriptor_register, master->abort_source, master->last_status);
    return false;
  }
  uint16_t length = read16(bytes), version = read16(bytes + 2);
  struct hid_i2c_descriptor captured = {
    .report_bytes = read16(bytes + 4), .report_register = read16(bytes + 6),
    .input_register = read16(bytes + 8), .input_bytes = read16(bytes + 10),
    .output_register = read16(bytes + 12), .output_bytes = read16(bytes + 14),
    .command_register = read16(bytes + 16), .data_register = read16(bytes + 18),
    .vendor = read16(bytes + 20), .product = read16(bytes + 22), .version = read16(bytes + 24),
  };
  if (length != sizeof(bytes) || version != HID_I2C_VERSION_1_00 ||
      !captured.report_bytes || !captured.report_register || !captured.input_register ||
      !captured.command_register || !captured.data_register || captured.input_bytes < 2 ||
      (captured.output_bytes && (!captured.output_register || captured.output_bytes < 2)) ||
      bytes[26] || bytes[27] || bytes[28] || bytes[29]) {
    kinfo("HID-I2C: malformed descriptor length=%u version=0x%x report=%u input=%u output=%u "
        "reserved=0x%02x%02x%02x%02x\n", length, version, captured.report_bytes, captured.input_bytes,
        captured.output_bytes, bytes[26], bytes[27], bytes[28], bytes[29]);
    return false;
  }
  /* Active registers are nonzero and distinct. The unused output register
   * may be zero when there are no output reports (HID-over-I2C 1.00). */
  const uint16_t registers[] = {
    captured.report_register, captured.input_register, captured.output_register,
    captured.command_register, captured.data_register,
  };
  for (size_t i = 0; i < sizeof(registers) / sizeof(registers[0]); ++i) {
    for (size_t j = 0; j < i; ++j) {
      if (registers[i] == registers[j]) {
        kinfo("HID-I2C: descriptor register IDs overlap: report=0x%x input=0x%x output=0x%x "
            "command=0x%x data=0x%x\n", captured.report_register, captured.input_register,
            captured.output_register, captured.command_register, captured.data_register);
        return false;
      }
    }
  }
  *descriptor = captured;
  kinfo("HID-I2C: descriptor v1.00 vendor=%04x product=%04x version=%04x report=%u@0x%x "
      "input=%u@0x%x output=%u@0x%x command=0x%x data=0x%x; no input enabled\n",
      captured.vendor, captured.product, captured.version, captured.report_bytes,
      captured.report_register, captured.input_bytes, captured.input_register,
      captured.output_bytes, captured.output_register, captured.command_register,
      captured.data_register);
  return true;
}

#define HID_I2C_TRANSFER_MS 100
#define HID_I2C_RESET_MS 5000
#define HID_I2C_REPORT_LIMIT 4096
#define HID_I2C_DRAIN_LIMIT 4
#define HID_I2C_MALFORMED_LIMIT 8
#define HID_I2C_OPCODE_RESET 1
#define HID_I2C_OPCODE_GET_REPORT 2
#define HID_I2C_OPCODE_SET_REPORT 3
#define HID_I2C_OPCODE_SET_POWER 8
#define HID_I2C_FEATURE_TYPE 3
#define HID_I2C_EXTENDED_ID 15
#define HID_I2C_PRECISION_MODE 3

/* This worker owns all buffers and state until reboot, including on refusal.
 * No controller/device pointer, report image or partial transfer is published. */
static struct hid_i2c_transport {
  struct dw_i2c_master *master;
  const struct acpi_hid_i2c_endpoint *endpoint;
  struct hid_i2c_descriptor descriptor;
  const struct gpio_input *interrupt;
  struct hid_report_layout layout;
  struct hid_touchpad_profile profile;
  struct hid_touchpad_frame frame;
  struct hid_pointer pointer;
  struct hid_i2c_metrics *metrics;
  uint8_t *report, *rx, *tx;
  size_t capacity;
  uint64_t reports, malformed, unknown_reports;
  const char *failure, *step;
  unsigned recovery_attempts;
  uint8_t contact_maximum;
  bool ready, precision, ownership_lost, clickpad;
} *transport;

static void write16(uint8_t *bytes, uint16_t value)
{
  bytes[0] = value;
  bytes[1] = value >> 8;
}

static bool transfer(struct hid_i2c_transport *state, const uint8_t *tx,
    size_t tx_bytes, uint8_t *rx, size_t rx_bytes, uint64_t deadline)
{
  uint64_t operation = task_deadline_after_ms(HID_I2C_TRANSFER_MS);
  if (operation > deadline) {
    operation = deadline;
  }
  enum dw_i2c_result result = dw_i2c_transfer(state->master, state->endpoint->address,
      state->endpoint->addressing_mode != 0, tx, tx_bytes, rx, rx_bytes, operation);
  if (result == DW_I2C_OK) {
    return true;
  }
  kinfo("HID-I2C: transfer refused: %s operation=%s step=%s abort=0x%x status=0x%x cleanup-failed=%u\n",
      dw_i2c_result_name(result), state->step, state->master->last_step, state->master->abort_source,
      state->master->last_status, state->master->failed);
  state->failure = "I2C transfer/cleanup";
  return false;
}

static bool asserted(struct hid_i2c_transport *state, bool *active)
{
  bool high;
  if (!gpio_input_level(state->interrupt, &high)) {
    state->ownership_lost = true;
    state->failure = "GPIO level or ownership unavailable";
    return false;
  }
  *active = state->endpoint->interrupt.polarity ? !high : high;
  return true;
}

static bool command(struct hid_i2c_transport *state, uint8_t opcode)
{
  uint8_t bytes[4];
  write16(bytes, state->descriptor.command_register);
  bytes[2] = 0; /* RESET arguments, or SET_POWER(on). */
  bytes[3] = opcode;
  state->step = "SET_POWER(on)";
  return transfer(state, bytes, sizeof(bytes), NULL, 0, UINT64_MAX);
}

static bool reset(struct hid_i2c_transport *state)
{
  if (!command(state, HID_I2C_OPCODE_SET_POWER)) {
    return false;
  }
  /* Pinned Linux compatibility settling delay; no repeated power cycle. */
  kernel_task_sleep_until(task_deadline_after_ms(60));
  uint64_t deadline = task_deadline_after_ms(HID_I2C_RESET_MS);
  uint8_t bytes[4];
  write16(bytes, state->descriptor.command_register);
  bytes[2] = 0;
  bytes[3] = HID_I2C_OPCODE_RESET;
  state->step = "RESET command";
  if (!transfer(state, bytes, sizeof(bytes), NULL, 0, deadline)) {
    return false;
  }
  while (!task_deadline_expired(deadline)) {
    bool active;
    if (!asserted(state, &active)) {
      return false;
    }
    if (active) {
      /* The normal input read has no register-address prefix. The zero
       * length announces reset completion; remaining transfer bytes are pad. */
      state->step = "RESET completion";
      if (!transfer(state, NULL, 0, state->rx, state->descriptor.input_bytes, deadline)) {
        return false;
      }
      if (read16(state->rx) != 0) {
        state->failure = "RESET completion was not zero length";
        return false;
      }
      kdebug("HID-I2C: RESET complete (zero length)\n");
      return true;
    }
    uint64_t next = task_deadline_after_ms(1);
    kernel_task_sleep_until(next < deadline ? next : deadline);
  }
  state->failure = "RESET completion deadline";
  return false;
}

static size_t report_bytes(const struct hid_i2c_transport *state,
    enum hid_layout_type type, uint8_t id)
{
  uint32_t bits = state->layout.bits[type][id];
  return bits ? 2 + state->layout.numbered[type] + ((uint64_t)bits + 7) / 8 : 0;
}

static bool admit_layout(struct hid_i2c_transport *state)
{
  uint8_t address[2];
  state->step = "report descriptor";
  write16(address, state->descriptor.report_register);
  if (!transfer(state, address, sizeof(address), state->report,
      state->descriptor.report_bytes, UINT64_MAX)) {
    return false;
  }
  const char *reason;
  if (!hid_layout_parse_touchpad(&state->layout, state->report,
      state->descriptor.report_bytes, &state->profile, &reason)) {
    kinfo("HID-I2C: report descriptor refused: %s\n", reason);
    state->failure = "report descriptor layout";
    return false;
  }
  bool input = false;
  for (unsigned type = 0; type < HID_LAYOUT_TYPES; ++type) {
    for (unsigned id = 0; id <= UINT8_MAX; ++id) {
      size_t bytes = report_bytes(state, type, id);
      if (!bytes) {
        continue;
      }
      if (bytes > state->capacity ||
          (type == HID_LAYOUT_INPUT && bytes > state->descriptor.input_bytes)) {
        state->failure = "report size exceeds descriptor or prepared budget";
        return false;
      }
      input |= type == HID_LAYOUT_INPUT;
      kdebug("HID-I2C: layout type=%u id=%u data-bits=%u wire-bytes=%zu\n",
          type, id, state->layout.bits[type][id], bytes);
    }
  }
  if (!input) {
    state->failure = "report descriptor has no input";
    return false;
  }
  kdebug("HID-I2C: report descriptor fetched bytes=%u; mode-feature=%u\n",
      state->descriptor.report_bytes, state->layout.mode_present);
  for (size_t i = 0; i < state->descriptor.report_bytes; i += 8) {
    /* One boot descriptor, never a stream of finger/report data. */
    uint8_t row[8] = {0};
    size_t bytes = state->descriptor.report_bytes - i;
    if (bytes > sizeof(row)) {
      bytes = sizeof(row);
    }
    memcpy(row, state->report + i, bytes);
    ktrace("HID-I2C descriptor: %zu/%zu %02x %02x %02x %02x %02x %02x %02x %02x\n",
        i, bytes, row[0], row[1], row[2], row[3], row[4], row[5], row[6], row[7]);
  }
  return true;
}

static size_t feature_command(struct hid_i2c_transport *state, uint8_t id, uint8_t opcode)
{
  uint8_t *bytes = state->tx;
  write16(bytes, state->descriptor.command_register);
  bytes[2] = HID_I2C_FEATURE_TYPE << 4 | (id >= HID_I2C_EXTENDED_ID ? HID_I2C_EXTENDED_ID : id);
  bytes[3] = opcode;
  size_t count = 4;
  if (id >= HID_I2C_EXTENDED_ID) {
    bytes[count++] = id;
  }
  write16(bytes + count, state->descriptor.data_register);
  return count + 2;
}

static bool get_feature(struct hid_i2c_transport *state, uint8_t id, size_t bytes)
{
  state->step = "GET_REPORT feature";
  size_t count = feature_command(state, id, HID_I2C_OPCODE_GET_REPORT);
  if (!transfer(state, state->tx, count, state->rx, bytes, UINT64_MAX)) {
    return false;
  }
  if (read16(state->rx) != bytes ||
      (state->layout.numbered[HID_LAYOUT_FEATURE] && state->rx[2] != id)) {
    state->failure = "incomplete feature reply or wrong report ID";
    return false;
  }
  return true;
}

static uint32_t mode_value(const struct hid_i2c_transport *state)
{
  const uint8_t *data = state->rx + 2 + state->layout.numbered[HID_LAYOUT_FEATURE];
  uint32_t value = 0;
  for (unsigned i = 0; i < state->layout.mode_width; ++i) {
    size_t bit = state->layout.mode_bit + i;
    value |= (uint32_t)((data[bit / 8] >> (bit % 8)) & 1) << i;
  }
  return value;
}

static bool precision_mode(struct hid_i2c_transport *state)
{
  if (!state->layout.mode_present) {
    state->failure = "missing descriptor-derived Input Mode";
    return false;
  }
  uint8_t id = state->layout.mode_id;
  size_t bytes = report_bytes(state, HID_LAYOUT_FEATURE, id);
  if (!bytes || !get_feature(state, id, bytes)) {
    state->failure = state->failure ? state->failure : "empty mode feature";
    return false;
  }
  uint32_t previous = mode_value(state);
  if (previous != HID_I2C_PRECISION_MODE) {
    uint8_t *data = state->rx + 2 + state->layout.numbered[HID_LAYOUT_FEATURE];
    for (unsigned i = 0; i < state->layout.mode_width; ++i) {
      size_t bit = state->layout.mode_bit + i;
      uint8_t mask = 1u << (bit % 8);
      data[bit / 8] = (data[bit / 8] & ~mask) |
          ((HID_I2C_PRECISION_MODE >> i & 1) ? mask : 0);
    }
    size_t count = feature_command(state, id, HID_I2C_OPCODE_SET_REPORT);
    state->step = "SET_REPORT mode feature";
    memcpy(state->tx + count, state->rx, bytes);
    if (!transfer(state, state->tx, count + bytes, NULL, 0, UINT64_MAX) ||
        !get_feature(state, id, bytes)) {
      return false;
    }
    if (mode_value(state) != HID_I2C_PRECISION_MODE) {
      state->failure = "Input Mode readback";
      return false;
    }
  }
  state->precision = true;
  kinfo("HID-I2C: Input Mode id=%u bit=%u width=%u previous=%u precision=3 verified\n",
      id, state->layout.mode_bit, state->layout.mode_width, previous);
  return true;
}

static bool feature_value(struct hid_i2c_transport *state,
    const struct hid_layout_field *field, uint32_t *value)
{
  size_t bytes = report_bytes(state, HID_LAYOUT_FEATURE, field->id);
  if (!field->present || !bytes || !get_feature(state, field->id, bytes)) {
    return false;
  }
  const uint8_t *data = state->rx + 2 + state->layout.numbered[HID_LAYOUT_FEATURE];
  uint32_t captured = 0;
  for (unsigned i = 0; i < field->width; ++i) {
    uint64_t bit = (uint64_t)field->bit + i;
    captured |= (uint32_t)((data[bit / 8] >> (bit % 8)) & 1) << i;
  }
  if ((int64_t)captured < field->minimum || (int64_t)captured > field->maximum) {
    return false;
  }
  *value = captured;
  return true;
}

static bool capabilities(struct hid_i2c_transport *state)
{
  uint32_t maximum, button = 0;
  if (!feature_value(state, &state->profile.count_maximum, &maximum) || !maximum ||
      maximum > state->profile.slot_count || maximum > state->profile.count.maximum ||
      (state->profile.button_type.present &&
       (!feature_value(state, &state->profile.button_type, &button) || button > 2))) {
    state->failure = "parallel contact capacity or button capability";
    return false;
  }
  state->contact_maximum = maximum;
  state->clickpad = state->profile.button_type.present && button == 0;
  kdebug("HID-I2C: parallel contacts=%u slots=%u button-type=%u; HID bytes=%zu id=%u\n",
      maximum, state->profile.slot_count, button, state->profile.report_bytes,
      state->profile.report_id);
  return true;
}

enum input_result { INPUT_VALID, INPUT_IGNORED, INPUT_MALFORMED, INPUT_RESET, INPUT_FAILED };

static enum input_result input_read(struct hid_i2c_transport *state)
{
  state->step = "input read";
  if (!transfer(state, NULL, 0, state->rx, state->descriptor.input_bytes, UINT64_MAX)) {
    return INPUT_FAILED;
  }
  uint16_t length = read16(state->rx);
  if (!length) {
    state->failure = "unexpected client RESET; mode and report state invalid";
    return INPUT_RESET;
  }
  uint8_t id = 0;
  const char *reason = "report length or ID";
  /* A nonempty HID image must fit the completed read. On numbered reports
   * its first byte is the ID; an unknown ID has no descriptor-derived size. */
  if (length <= 2 || length > state->descriptor.input_bytes) {
    goto malformed;
  }
  id = state->layout.numbered[HID_LAYOUT_INPUT] ? state->rx[2] : 0;
  if (!state->layout.bits[HID_LAYOUT_INPUT][id]) {
    ++state->unknown_reports;
    return INPUT_IGNORED;
  }
  if (length != report_bytes(state, HID_LAYOUT_INPUT, id)) {
    goto malformed;
  }
  /* Compatibility mouse/vendor reports never become a second pointer path. */
  if (id != state->profile.report_id) {
    return INPUT_IGNORED;
  }
  if (!hid_touchpad_decode(&state->profile, state->rx + 2, length - 2,
      &state->frame, &reason)) {
    goto malformed;
  }
  if (state->frame.count > state->contact_maximum) {
    reason = "contact count exceeds live capability";
    goto malformed;
  }
  ++state->reports;
  return INPUT_VALID;
malformed:
  if (!state->malformed) {
    kdebug("HID-I2C: first malformed input: length=%u id=%u expected=%zu max=%u reason=%s\n",
        length, id, report_bytes(state, HID_LAYOUT_INPUT, id),
        state->descriptor.input_bytes, reason);
  }
  ++state->malformed;
  memset(&state->frame, 0, sizeof(state->frame));
  return INPUT_MALFORMED;
}

static bool initialize_client(struct hid_i2c_transport *state, bool initial)
{
  state->failure = NULL;
  state->precision = false;
  memset(&state->frame, 0, sizeof(state->frame));
  if (!initial) {
    struct hid_i2c_descriptor descriptor;
    state->step = "recovery descriptor";
    if (!hid_i2c_descriptor_read(state->master, state->endpoint, &descriptor)) {
      state->failure = "recovery descriptor read/validation";
      return false;
    }
    if (descriptor.vendor != state->descriptor.vendor ||
        descriptor.product != state->descriptor.product ||
        descriptor.version != state->descriptor.version ||
        descriptor.report_bytes > state->capacity || descriptor.input_bytes > state->capacity) {
      state->failure = "client identity changed or prepared capacity exceeded";
      return false;
    }
    state->descriptor = descriptor;
  }
  if (!reset(state) || !admit_layout(state) || !capabilities(state) || !precision_mode(state)) {
    return false;
  }
  if (!hid_pointer_prepare(&state->pointer, &state->profile, state->clickpad)) {
    state->failure = "physical geometry exceeds filter capacity";
    return false;
  }
  state->ready = true;
  hid_pointer_ready(&state->pointer);
  return true;
}

static bool recover_client(struct hid_i2c_transport *state, bool uncertain)
{
  state->ready = false;
  hid_i2c_metrics_recovery(state->metrics);
  hid_pointer_lost(&state->pointer, uncertain);
  memset(&state->frame, 0, sizeof(state->frame));
  kinfo("HID-I2C: input quarantined: %s; client recovery pending\n",
      state->failure ? state->failure : "lost client state");
  for (;;) {
    /* The fast budget is boot-retained across successes and later flaps.
     * There is no controller/pad reset and no clearing of master->failed. */
    if (state->ownership_lost || !dw_i2c_quiescent(state->master)) {
      state->failure = "client recovery refused: ownership or bus cleanup unproven";
      return false;
    }
    static const uint32_t delays[] = {250, 1000, 4000};
    uint32_t delay = state->recovery_attempts < 3 ? delays[state->recovery_attempts++] : 30000;
    kdebug("HID-I2C: client retry in %u ms fast-budget-used=%u/3\n",
        delay, state->recovery_attempts);
    kernel_task_sleep_until(task_deadline_after_ms(delay));
    bool active;
    if (!asserted(state, &active) || !dw_i2c_quiescent(state->master)) {
      state->failure = "client recovery refused: ownership or bus cleanup changed";
      return false;
    }
    if (initialize_client(state, false)) {
      kinfo("HID-I2C: client reinitialized; mode 3 verified, %s\n",
          state->pointer.needs_lift ? "fresh lift required" : "first touch rebaselines");
      return true;
    }
  }
}

void hid_i2c_poll(struct dw_i2c_master *master,
    const struct acpi_hid_i2c_endpoint *endpoint,
    const struct hid_i2c_descriptor *descriptor, const struct gpio_input *interrupt)
{
  if (transport) {
    return;
  }
  if (!interrupt || endpoint->interrupt.kind != ACPI_HID_I2C_GPIO ||
      endpoint->interrupt.triggering != 0 || endpoint->interrupt.polarity > 1) {
    kinfo("HID-I2C: polling refused: no qualified level GPIO; no client command\n");
    return;
  }
  if (descriptor->report_bytes > HID_I2C_REPORT_LIMIT ||
      descriptor->input_bytes > HID_I2C_REPORT_LIMIT) {
    kinfo("HID-I2C: polling refused: descriptor exceeds %u-byte prepared report budget\n",
        HID_I2C_REPORT_LIMIT);
    return;
  }
  uint64_t flags = cpu_save_interrupts();
  struct hid_i2c_transport *state = kmalloc(sizeof(*state));
  if (state) {
    *state = (struct hid_i2c_transport){.master = master, .endpoint = endpoint,
      .descriptor = *descriptor, .interrupt = interrupt, .capacity = HID_I2C_REPORT_LIMIT};
    state->report = kmalloc(state->capacity);
    state->rx = kmalloc(state->capacity);
    state->tx = kmalloc(state->capacity + 7); /* Maximum extended-ID command header. */
    if (!state->report || !state->rx || !state->tx) {
      kfree(state->report);
      kfree(state->rx);
      kfree(state->tx);
      kfree(state);
      state = NULL;
    }
  }
  cpu_restore_interrupts(flags);
  if (!state) {
    kinfo("HID-I2C: polling refused: transport allocation\n");
    return;
  }
  transport = state;
  if (!initialize_client(state, true) && !recover_client(state, false)) {
    goto failed;
  }
  state->metrics = hid_i2c_metrics_create();
  kinfo("HID-I2C: polling ready; input=%u mode=3, source-owned precision input\n",
      state->descriptor.input_bytes);
  unsigned malformed = 0;
  for (;;) {
    uint32_t interval = hid_i2c_metrics_interval(state->metrics);
    uint64_t start = arch_monotonic_ns();
    unsigned frames = 0;
    bool active = false, recovered = false;
    if (interval) {
      for (unsigned i = 0; i < HID_I2C_DRAIN_LIMIT; ++i) {
        if (!asserted(state, &active)) {
          goto failed;
        }
        if (!active) {
          break;
        }
        enum input_result result = input_read(state);
        if (result == INPUT_FAILED || result == INPUT_RESET) {
          if (!recover_client(state, result == INPUT_FAILED)) {
            goto failed;
          }
          malformed = 0;
          recovered = true;
          break;
        }
        if (result == INPUT_VALID) {
          ++frames;
          malformed = 0;
          hid_pointer_report(&state->pointer, &state->frame);
          hid_i2c_metrics_report(state->metrics,
              state->layout.numbered[HID_LAYOUT_INPUT] ? state->rx[2] : 0);
        } else if (result == INPUT_MALFORMED) {
          /* Unknown contact/button state never fabricates a lift or preserves
           * stale deltas. Physical held switches rebaseline independently. */
          hid_pointer_lost(&state->pointer, true);
          if (++malformed == HID_I2C_MALFORMED_LIMIT) {
            state->failure = "consecutive malformed reports";
            if (!recover_client(state, true)) {
              goto failed;
            }
            malformed = 0;
            recovered = true;
            break;
          }
        }
      }
    }
    if (interval && active && !asserted(state, &active)) {
      goto failed;
    }
    uint64_t end = arch_monotonic_ns();
    hid_i2c_metrics_poll(state->metrics, start, end, interval != 0 && !recovered, frames, active,
        state->malformed);
    /* Never catch up with a spinning loop. Deadlines may be late under load. */
    uint64_t next = start + (uint64_t)(interval ? interval : 8) * 1000000;
    kernel_task_sleep_until(next > end ? next : task_deadline_after_ms(1));
  }
failed:
  state->ready = false;
  hid_pointer_lost(&state->pointer, true);
  kinfo("HID-I2C: transport disabled until reboot: %s operation=%s; ownership retained\n",
      state->failure ? state->failure : "unspecified refusal", state->step ? state->step : "preparation");
}
