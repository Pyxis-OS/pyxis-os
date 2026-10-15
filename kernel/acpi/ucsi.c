#include <arch/clock.h>
#include <kernel/log.h>
#include <uacpi/namespace.h>
#include <uacpi/notify.h>
#include <uacpi/resources.h>
#include <uacpi/types.h>
#include <uacpi/uacpi.h>
#include <uacpi/utilities.h>
#include "host.h"

/* USB Type-C Connector System Software Interface (UCSI) client for the ACPI
 * transport: the PNP0CA0 device's memory mailbox, plus _DSM functions that copy
 * CONTROL out to the platform (write) and CCI/MESSAGE IN back (refresh).
 * Version 1.0 only, the layout this client was checked against. It runs one
 * command at a time from the ACPI worker's top level, reads connector status
 * and acknowledges it, and requests nothing else: no role, power, mode or
 * connector-reset command. References: UCSI 1.2 (January 2020) sections 3,
 * 4 and 4.5.4-4.5.6, 4.5.17. */

#define UCSI_ID "PNP0CA0"
#define UCSI_VERSION_1_0 0x0100u
#define UCSI_STATUS_CHANGE_NOTIFY 0x80u

/* _DSM UUID 6f8398c2-7ca4-11e4-ad36-631042b5008f, in ACPI's mixed-endian order. */
static const uint8_t ucsi_dsm_uuid[] = {
  0xc2, 0x98, 0x83, 0x6f, 0xa4, 0x7c, 0xe4, 0x11,
  0xad, 0x36, 0x63, 0x10, 0x42, 0xb5, 0x00, 0x8f,
};
#define UCSI_DSM_REVISION 1u
#define UCSI_DSM_QUERY 0u
#define UCSI_DSM_WRITE 1u
#define UCSI_DSM_REFRESH 2u

/* Mailbox byte offsets (section 3). MESSAGE OUT is never used. */
#define UCSI_VERSION 0u
#define UCSI_CCI 4u
#define UCSI_CONTROL 8u
#define UCSI_MESSAGE_IN 16u
#define UCSI_MESSAGE_IN_BYTES 16u
#define UCSI_MAILBOX_BYTES (UCSI_MESSAGE_IN + UCSI_MESSAGE_IN_BYTES)

/* CCI (section 3.2). */
#define UCSI_CCI_CONNECTOR_SHIFT 1u
#define UCSI_CCI_CONNECTOR_MASK 0x7fu
#define UCSI_CCI_LENGTH_SHIFT 8u
#define UCSI_CCI_LENGTH_MASK 0xffu
#define UCSI_CCI_CONNECTOR_FIELD (UCSI_CCI_CONNECTOR_MASK << UCSI_CCI_CONNECTOR_SHIFT)
#define UCSI_CCI_LENGTH_FIELD (UCSI_CCI_LENGTH_MASK << UCSI_CCI_LENGTH_SHIFT)
#define UCSI_CCI_NOT_SUPPORTED (UINT32_C(1) << 25)
#define UCSI_CCI_RESET_COMPLETE (UINT32_C(1) << 27)
#define UCSI_CCI_BUSY (UINT32_C(1) << 28)
#define UCSI_CCI_ACK_COMPLETE (UINT32_C(1) << 29)
#define UCSI_CCI_ERROR (UINT32_C(1) << 30)
#define UCSI_CCI_COMMAND_COMPLETE (UINT32_C(1) << 31)

/* CONTROL commands and their fields (section 4.5). */
#define UCSI_PPM_RESET 0x01u
#define UCSI_ACK_CC_CI 0x04u
#define UCSI_SET_NOTIFICATION_ENABLE 0x05u
#define UCSI_GET_CAPABILITY 0x06u
#define UCSI_GET_CONNECTOR_STATUS 0x12u
#define UCSI_CONNECTOR_SHIFT 16u
#define UCSI_ACK_CONNECTOR_CHANGE (UINT64_C(1) << 16)
#define UCSI_ACK_COMMAND_COMPLETE (UINT64_C(1) << 17)
#define UCSI_NOTIFY_COMMAND_COMPLETE (UINT64_C(1) << 16)
#define UCSI_NOTIFY_CONNECT_CHANGE (UINT64_C(1) << 30)

/* GET_CAPABILITY and GET_CONNECTOR_STATUS replies (tables 4-13 and 4-42). */
#define UCSI_CAPABILITY_BYTES 16u
#define UCSI_CAPABILITY_ATTRIBUTES 0u
#define UCSI_CAPABILITY_CONNECTORS 4u
#define UCSI_CAPABILITY_CONNECTORS_MASK 0x7fu
#define UCSI_CAPABILITY_FEATURES 5u
#define UCSI_STATUS_BYTES 9u
#define UCSI_STATUS_CHANGE 0u
#define UCSI_STATUS_FLAGS 2u
#define UCSI_STATUS_REQUEST 4u
#define UCSI_STATUS_BATTERY 8u
#define UCSI_STATUS_MODE_MASK 0x7u
#define UCSI_STATUS_CONNECTED (1u << 3)
#define UCSI_STATUS_PROVIDER (1u << 4)
#define UCSI_STATUS_PARTNER_FLAGS_SHIFT 5u
#define UCSI_STATUS_PARTNER_FLAGS_MASK 0xffu
#define UCSI_STATUS_PARTNER_TYPE_SHIFT 13u
#define UCSI_STATUS_PARTNER_TYPE_MASK 0x7u
#define UCSI_STATUS_BATTERY_MASK 0x3u

/* Linux's ordinary completion and reset budgets; polls refresh CCI through
 * the _DSM only while a command is outstanding. */
#define UCSI_COMMAND_NS UINT64_C(5000000000)
#define UCSI_RESET_NS UINT64_C(10000000000)
#define UCSI_REFRESH_NS UINT64_C(20000000)
/* How long to wait for firmware to publish VERSION, which only its EC query
 * copies into the mailbox. */
#define UCSI_VERSION_NS UINT64_C(10000000000)
/* More connector changes than this within the window start a back-off: at
 * most one change per second, each still acknowledged, until none has been
 * handled for the quiet period. Entry and exit are logged at most once a minute. */
#define UCSI_CHANGE_LIMIT 16u
#define UCSI_CHANGE_WINDOW_NS UINT64_C(10000000000)
#define UCSI_BACKOFF_INTERVAL_NS UINT64_C(1000000000)
#define UCSI_BACKOFF_QUIET_NS UINT64_C(10000000000)
#define UCSI_BACKOFF_LOG_NS UINT64_C(60000000000)
#define UCSI_CONNECTOR_LIMIT 8u

enum ucsi_phase {
  UCSI_ABSENT,
  UCSI_VERSION_WAIT,
  UCSI_RESETTING,
  UCSI_COMMAND,
  UCSI_ACKNOWLEDGING,
  UCSI_IDLE,
  UCSI_PARKED,
};

/* What the outstanding ordinary command is for. */
enum ucsi_step {
  UCSI_STEP_COMMAND_NOTIFICATION,
  UCSI_STEP_CAPABILITY,
  UCSI_STEP_CONNECT_NOTIFICATION,
  UCSI_STEP_INITIAL_STATUS,
  UCSI_STEP_CHANGE_STATUS,
};

struct ucsi_connector_status {
  uint16_t change;
  uint8_t mode, partner_flags, partner_type, battery;
  bool connected, provider;
  uint32_t request;
};

/* Worker only. The mapping and namespace node stay valid until reboot. */
static struct {
  uacpi_namespace_node *node;
  volatile uint8_t *mailbox;
  enum ucsi_phase phase;
  enum ucsi_step step;
  uint64_t control;
  uint8_t connector, connectors;
  /* Notify(0x80) since the last turn; firmware has refreshed the mailbox. */
  bool notified;
  /* The command's completion was rejected; park once it is acknowledged. */
  const char *failure;
  uint32_t cci;
  uint64_t sent, deadline, next_refresh;
  uint64_t change_window, last_change, next_change, idle_due;
  unsigned changes;
  bool backoff, backoff_logged;
  uint64_t backoff_changes, next_backoff_log;
  unsigned backoffs;
  struct ucsi_connector_status status[UCSI_CONNECTOR_LIMIT];
} ucsi;

static const char *const phase_names[] = {
  [UCSI_ABSENT] = "absent",
  [UCSI_VERSION_WAIT] = "version",
  [UCSI_RESETTING] = "PPM reset",
  [UCSI_COMMAND] = "command",
  [UCSI_ACKNOWLEDGING] = "ACK_CC_CI",
  [UCSI_IDLE] = "idle",
  [UCSI_PARKED] = "parked",
};

static uint32_t read_cci(void)
{
  const volatile uint8_t *cci = ucsi.mailbox + UCSI_CCI;
  return (uint32_t)cci[0] | (uint32_t)cci[1] << 8 | (uint32_t)cci[2] << 16 |
      (uint32_t)cci[3] << 24;
}

static unsigned cci_connector(uint32_t cci)
{
  return (cci >> UCSI_CCI_CONNECTOR_SHIFT) & UCSI_CCI_CONNECTOR_MASK;
}

static unsigned cci_length(uint32_t cci)
{
  return (cci >> UCSI_CCI_LENGTH_SHIFT) & UCSI_CCI_LENGTH_MASK;
}

static void park(const char *reason)
{
  kwarn("ACPI: UCSI parked: %s (phase %s, CONTROL 0x%llx, CCI 0x%x); no further commands\n",
      reason, phase_names[ucsi.phase], (unsigned long long)ucsi.control, (unsigned)ucsi.cci);
  ucsi.phase = UCSI_PARKED;
}

static uacpi_status evaluate_dsm(unsigned function, uacpi_object **result)
{
  uacpi_object *objects[] = {
    uacpi_object_create_buffer((uacpi_data_view){
      .const_bytes = ucsi_dsm_uuid, .length = sizeof(ucsi_dsm_uuid),
    }),
    uacpi_object_create_integer(UCSI_DSM_REVISION),
    uacpi_object_create_integer(function),
    uacpi_object_create_package((uacpi_object_array){0}),
  };
  uacpi_status status = UACPI_STATUS_OUT_OF_MEMORY;
  if (objects[0] && objects[1] && objects[2] && objects[3]) {
    status = uacpi_eval_typed(ucsi.node, "_DSM",
        &(uacpi_object_array){.objects = objects, .count = 4}, UACPI_OBJECT_BUFFER_BIT, result);
  }
  for (size_t i = 0; i < 4; ++i) {
    if (objects[i]) {
      uacpi_object_unref(objects[i]);
    }
  }
  return status;
}

/* Write and refresh return a buffer whose contents carry no result. */
static bool call_dsm(unsigned function)
{
  uacpi_object *result = UACPI_NULL;
  uacpi_status status = evaluate_dsm(function, &result);
  if (result) {
    uacpi_object_unref(result);
  }
  if (status != UACPI_STATUS_OK) {
    kwarn("ACPI: UCSI _DSM function %u failed: %s\n", function, uacpi_status_to_string(status));
    return false;
  }
  return true;
}

static bool dsm_functions_present(void)
{
  uacpi_object *result = UACPI_NULL;
  uacpi_data_view mask;
  unsigned required = 1u << UCSI_DSM_QUERY | 1u << UCSI_DSM_WRITE | 1u << UCSI_DSM_REFRESH;
  bool present = evaluate_dsm(UCSI_DSM_QUERY, &result) == UACPI_STATUS_OK &&
      uacpi_object_get_buffer(result, &mask) == UACPI_STATUS_OK && mask.length &&
      (mask.const_bytes[0] & required) == required;
  if (result) {
    uacpi_object_unref(result);
  }
  return present;
}

/* Refreshes the mailbox through the _DSM and samples CCI. */
static bool refresh(void)
{
  if (!call_dsm(UCSI_DSM_REFRESH)) {
    return false;
  }
  ucsi.cci = read_cci();
  return true;
}

/* Starts COMMAND. EXPECTED is the CCI bit that completes it; the sample this
 * command follows must not already show it, or a stale value could be taken
 * for this command's completion. */
static void send(uint64_t command, enum ucsi_phase phase, uint32_t expected, uint64_t budget)
{
  if (ucsi.cci & expected) {
    park("completion already indicated before the command; cannot correlate");
    return;
  }
  volatile uint8_t *control = ucsi.mailbox + UCSI_CONTROL;
  for (unsigned i = 0; i < sizeof(command); ++i) {
    control[i] = (uint8_t)(command >> (8 * i));
  }
  ucsi.control = command;
  ucsi.phase = phase;
  ucsi.sent = arch_monotonic_ns();
  ucsi.deadline = ucsi.sent + budget;
  ucsi.next_refresh = ucsi.sent + UCSI_REFRESH_NS;
  ktrace("ACPI: UCSI CONTROL 0x%llx after CCI 0x%x\n", (unsigned long long)command,
      (unsigned)ucsi.cci);
  if (!call_dsm(UCSI_DSM_WRITE)) {
    park("CONTROL write failed");
  }
}

static void send_command(uint64_t command, enum ucsi_step step)
{
  ucsi.step = step;
  ucsi.failure = NULL;
  send(command, UCSI_COMMAND, UCSI_CCI_COMMAND_COMPLETE, UCSI_COMMAND_NS);
}

static void send_status(enum ucsi_step step, unsigned connector)
{
  ucsi.connector = (uint8_t)connector;
  send_command(UCSI_GET_CONNECTOR_STATUS | (uint64_t)connector << UCSI_CONNECTOR_SHIFT, step);
}

static void send_ack(bool connector_change)
{
  uint64_t command = UCSI_ACK_CC_CI | UCSI_ACK_COMMAND_COMPLETE;
  if (connector_change) {
    command |= UCSI_ACK_CONNECTOR_CHANGE;
  }
  send(command, UCSI_ACKNOWLEDGING, UCSI_CCI_ACK_COMPLETE, UCSI_COMMAND_NS);
}

static void copy_message(uint8_t *data, unsigned length)
{
  for (unsigned i = 0; i < length; ++i) {
    data[i] = ucsi.mailbox[UCSI_MESSAGE_IN + i];
  }
}

static uint32_t le32(const uint8_t *bytes)
{
  return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 |
      (uint32_t)bytes[3] << 24;
}

/* A completed command whose result is rejected: acknowledge only its
 * completion, so the PPM is not left waiting, then park. A connector change
 * stays unacknowledged. */
static void reject_completion(const char *reason)
{
  kwarn("ACPI: UCSI CONTROL 0x%llx rejected: %s (CCI 0x%x); acknowledging its completion only\n",
      (unsigned long long)ucsi.control, reason, (unsigned)ucsi.cci);
  ucsi.failure = reason;
  send_ack(false);
}

/* Validates and retains GET_CAPABILITY. Returns false once rejected. */
static bool take_capability(void)
{
  uint8_t data[UCSI_CAPABILITY_BYTES];
  if (cci_length(ucsi.cci) != UCSI_CAPABILITY_BYTES) {
    reject_completion("GET_CAPABILITY length is not 16");
    return false;
  }
  copy_message(data, sizeof(data));
  unsigned connectors = data[UCSI_CAPABILITY_CONNECTORS] & UCSI_CAPABILITY_CONNECTORS_MASK;
  if (!connectors || connectors > UCSI_CONNECTOR_LIMIT) {
    reject_completion("connector count outside 1-8");
    return false;
  }
  ucsi.connectors = (uint8_t)connectors;
  uint32_t features = data[UCSI_CAPABILITY_FEATURES] |
      (uint32_t)data[UCSI_CAPABILITY_FEATURES + 1] << 8 |
      (uint32_t)data[UCSI_CAPABILITY_FEATURES + 2] << 16;
  kdebug("ACPI: UCSI capability connectors=%u attributes=0x%x features=0x%x\n", connectors,
      (unsigned)le32(data + UCSI_CAPABILITY_ATTRIBUTES), (unsigned)features);
  return true;
}

/* Validates and retains GET_CONNECTOR_STATUS. Returns false once rejected. */
static bool take_status(void)
{
  uint8_t data[UCSI_STATUS_BYTES];
  if (cci_length(ucsi.cci) != UCSI_STATUS_BYTES) {
    reject_completion("GET_CONNECTOR_STATUS length is not 9");
    return false;
  }
  copy_message(data, sizeof(data));
  unsigned flags = data[UCSI_STATUS_FLAGS] | (unsigned)data[UCSI_STATUS_FLAGS + 1] << 8;
  struct ucsi_connector_status *status = &ucsi.status[ucsi.connector - 1];
  *status = (struct ucsi_connector_status){
    .change = (uint16_t)(data[UCSI_STATUS_CHANGE] | data[UCSI_STATUS_CHANGE + 1] << 8),
    .mode = (uint8_t)(flags & UCSI_STATUS_MODE_MASK),
    .connected = (flags & UCSI_STATUS_CONNECTED) != 0,
    .provider = (flags & UCSI_STATUS_PROVIDER) != 0,
    .partner_flags = (uint8_t)((flags >> UCSI_STATUS_PARTNER_FLAGS_SHIFT) &
        UCSI_STATUS_PARTNER_FLAGS_MASK),
    .partner_type = (uint8_t)((flags >> UCSI_STATUS_PARTNER_TYPE_SHIFT) &
        UCSI_STATUS_PARTNER_TYPE_MASK),
    .request = le32(data + UCSI_STATUS_REQUEST),
    .battery = data[UCSI_STATUS_BATTERY] & UCSI_STATUS_BATTERY_MASK,
  };
  kdebug("ACPI: UCSI connector %u %s: change=0x%x connected=%u mode=%u provider=%u "
      "partner-type=%u partner-flags=0x%x battery=%u RDO=0x%x\n", ucsi.connector,
      ucsi.step == UCSI_STEP_CHANGE_STATUS ? "change" : "initial", status->change,
      status->connected, status->mode, status->provider, status->partner_type,
      status->partner_flags, status->battery, (unsigned)status->request);
  return true;
}

static void enter_backoff(uint64_t now)
{
  ucsi.backoff = true;
  ucsi.backoff_changes = 0;
  ucsi.next_change = now + UCSI_BACKOFF_INTERVAL_NS;
  ++ucsi.backoffs;
  ucsi.backoff_logged = now >= ucsi.next_backoff_log;
  if (ucsi.backoff_logged) {
    ucsi.next_backoff_log = now + UCSI_BACKOFF_LOG_NS;
    kinfo("ACPI: UCSI: more than 16 connector changes within 10 s; handling at most one per "
        "second (back-off %u since boot)\n", ucsi.backoffs);
  }
}

static void leave_backoff(void)
{
  ucsi.backoff = false;
  ucsi.change_window = 0;
  if (ucsi.backoff_logged) {
    kinfo("ACPI: UCSI: connector changes quiet for 10 s; back-off ended after %llu changes\n",
        (unsigned long long)ucsi.backoff_changes);
  }
}

/* Idle: handles a connector change the PPM reports in CCI outside a command,
 * pacing changes during a back-off. Returns when idle work is next due. */
static uint64_t idle_due(uint64_t now)
{
  unsigned connector = (ucsi.cci & UCSI_CCI_BUSY) ? 0 : cci_connector(ucsi.cci);
  if (!connector) {
    if (ucsi.backoff && now - ucsi.last_change >= UCSI_BACKOFF_QUIET_NS) {
      leave_backoff();
    }
    return ucsi.backoff ? ucsi.last_change + UCSI_BACKOFF_QUIET_NS : UINT64_MAX;
  }
  if (connector > ucsi.connectors) {
    park("connector change on a connector outside the capability count");
    return UINT64_MAX;
  }
  if (!ucsi.backoff) {
    if (now - ucsi.change_window >= UCSI_CHANGE_WINDOW_NS) {
      ucsi.change_window = now;
      ucsi.changes = 0;
    }
    if (++ucsi.changes > UCSI_CHANGE_LIMIT) {
      enter_backoff(now);
    }
  }
  if (ucsi.backoff) {
    if (now < ucsi.next_change) {
      return ucsi.next_change;
    }
    ucsi.next_change = now + UCSI_BACKOFF_INTERVAL_NS;
    ++ucsi.backoff_changes;
  }
  ucsi.last_change = now;
  send_status(UCSI_STEP_CHANGE_STATUS, connector);
  return UINT64_MAX;
}

static void idle_step(uint64_t now)
{
  ucsi.idle_due = idle_due(now);
}

static void command_complete(void)
{
  if (ucsi.cci & UCSI_CCI_NOT_SUPPORTED) {
    reject_completion("command not supported");
    return;
  }
  if (ucsi.cci & UCSI_CCI_ERROR) {
    reject_completion("command failed");
    return;
  }
  bool taken = true;
  switch (ucsi.step) {
  case UCSI_STEP_COMMAND_NOTIFICATION:
  case UCSI_STEP_CONNECT_NOTIFICATION:
    if (cci_length(ucsi.cci)) {
      reject_completion("SET_NOTIFICATION_ENABLE returned data");
      taken = false;
    }
    break;
  case UCSI_STEP_CAPABILITY:
    taken = take_capability();
    break;
  case UCSI_STEP_INITIAL_STATUS:
  case UCSI_STEP_CHANGE_STATUS:
    taken = take_status();
    break;
  }
  if (!taken) {
    return;
  }
  /* Only a status read made for the reported change consumes it; the initial
   * reads leave any indication for the change path. */
  bool change = ucsi.step == UCSI_STEP_CHANGE_STATUS &&
      cci_connector(ucsi.cci) == ucsi.connector;
  send_ack(change);
}

static void ack_complete(void)
{
  if (ucsi.failure) {
    park(ucsi.failure);
    return;
  }
  switch (ucsi.step) {
  case UCSI_STEP_COMMAND_NOTIFICATION:
    send_command(UCSI_GET_CAPABILITY, UCSI_STEP_CAPABILITY);
    return;
  case UCSI_STEP_CAPABILITY:
    send_command(UCSI_SET_NOTIFICATION_ENABLE | UCSI_NOTIFY_COMMAND_COMPLETE |
        UCSI_NOTIFY_CONNECT_CHANGE, UCSI_STEP_CONNECT_NOTIFICATION);
    return;
  case UCSI_STEP_CONNECT_NOTIFICATION:
    send_status(UCSI_STEP_INITIAL_STATUS, 1);
    return;
  case UCSI_STEP_INITIAL_STATUS:
    if (ucsi.connector < ucsi.connectors) {
      send_status(UCSI_STEP_INITIAL_STATUS, ucsi.connector + 1u);
      return;
    }
    kinfo("ACPI: UCSI 1.0 ready: %u connectors; command-completion and connect-change "
        "notifications\n", ucsi.connectors);
    break;
  case UCSI_STEP_CHANGE_STATUS:
    break;
  }
  ucsi.phase = UCSI_IDLE;
  idle_step(arch_monotonic_ns());
}

/* Completion shapes (UCSI 1.2 table 3-2 and each command's status table). A
 * reset completion and a busy indication set no other bit. An ACK completion
 * may carry only a connector change. A command completion may carry a
 * connector change, data length, error or not-supported; never reset, busy,
 * cancel or ACK completion. A failed command returns no data, and no reply
 * here exceeds MESSAGE IN. */
static bool valid_completion(uint32_t cci)
{
  switch (ucsi.phase) {
  case UCSI_RESETTING:
    return cci == UCSI_CCI_RESET_COMPLETE;
  case UCSI_ACKNOWLEDGING:
    return (cci & ~UCSI_CCI_CONNECTOR_FIELD) == UCSI_CCI_ACK_COMPLETE;
  case UCSI_COMMAND: {
    uint32_t allowed = UCSI_CCI_COMMAND_COMPLETE | UCSI_CCI_ERROR | UCSI_CCI_NOT_SUPPORTED |
        UCSI_CCI_CONNECTOR_FIELD | UCSI_CCI_LENGTH_FIELD;
    bool failed = cci & (UCSI_CCI_ERROR | UCSI_CCI_NOT_SUPPORTED);
    return !(cci & ~allowed) && cci_length(cci) <= UCSI_MESSAGE_IN_BYTES &&
        !(failed && cci_length(cci));
  }
  default:
    return false;
  }
}

static const uint32_t phase_completion[] = {
  [UCSI_RESETTING] = UCSI_CCI_RESET_COMPLETE,
  [UCSI_COMMAND] = UCSI_CCI_COMMAND_COMPLETE,
  [UCSI_ACKNOWLEDGING] = UCSI_CCI_ACK_COMPLETE,
};

static void park_expired(void)
{
  park(ucsi.phase == UCSI_RESETTING ? "PPM reset timed out" : "completion timed out");
}

/* Applies the latest CCI sample to the outstanding command or reset. Earlier
 * values without this phase's completion bit are not completions and are
 * waited out; a busy PPM completes later. The deadline is checked with a fresh
 * clock after the sample, which may follow slow AML, so an expired command is
 * never accepted. */
static void advance(void)
{
  uint64_t now = arch_monotonic_ns();
  if (now >= ucsi.deadline) {
    park_expired();
    return;
  }
  if (ucsi.cci & UCSI_CCI_BUSY) {
    if (ucsi.cci != UCSI_CCI_BUSY) {
      park("busy CCI with other bits set");
    }
    return;
  }
  if (!(ucsi.cci & phase_completion[ucsi.phase])) {
    return;
  }
  if (!valid_completion(ucsi.cci)) {
    park("completion CCI outside the UCSI definition");
    return;
  }
  uint64_t elapsed_us = (now - ucsi.sent) / 1000;
  switch (ucsi.phase) {
  case UCSI_RESETTING:
    ktrace("ACPI: UCSI PPM reset complete after %llu us\n", (unsigned long long)elapsed_us);
    send_command(UCSI_SET_NOTIFICATION_ENABLE | UCSI_NOTIFY_COMMAND_COMPLETE,
        UCSI_STEP_COMMAND_NOTIFICATION);
    break;
  case UCSI_COMMAND:
    ktrace("ACPI: UCSI CONTROL 0x%llx complete after %llu us, CCI 0x%x\n",
        (unsigned long long)ucsi.control, (unsigned long long)elapsed_us, (unsigned)ucsi.cci);
    command_complete();
    break;
  case UCSI_ACKNOWLEDGING:
    ktrace("ACPI: UCSI ACK 0x%llx complete after %llu us, CCI 0x%x\n",
        (unsigned long long)ucsi.control, (unsigned long long)elapsed_us, (unsigned)ucsi.cci);
    ack_complete();
    break;
  default:
    break;
  }
}

/* One PPM interface reset; never CONNECTOR_RESET. Its pre-command check needs
 * a fresh CCI. */
static void begin_reset(void)
{
  if (!refresh()) {
    park("CCI refresh failed");
    return;
  }
  ucsi.step = UCSI_STEP_COMMAND_NOTIFICATION;
  send(UCSI_PPM_RESET, UCSI_RESETTING, UCSI_CCI_RESET_COMPLETE, UCSI_RESET_NS);
}

/* VERSION is copied by firmware with each EC query. Zero means not yet. */
static void check_version(uint64_t now)
{
  unsigned version = ucsi.mailbox[UCSI_VERSION] | (unsigned)ucsi.mailbox[UCSI_VERSION + 1] << 8;
  if (version == UCSI_VERSION_1_0) {
    begin_reset();
  } else if (version) {
    kinfo("ACPI: UCSI version 0x%x is not supported; only 1.0 is\n", version);
    ucsi.phase = UCSI_ABSENT;
  } else if (now >= ucsi.deadline) {
    park("firmware published no VERSION");
  }
}

static uacpi_status ucsi_notify(uacpi_handle context, uacpi_namespace_node *node,
                                uacpi_u64 value)
{
  (void)context;
  acpi_require_worker();
  if (node == ucsi.node && value == UCSI_STATUS_CHANGE_NOTIFY) {
    ucsi.notified = true;
  }
  return UACPI_STATUS_OK;
}

static uacpi_iteration_decision find_mailbox(void *user, uacpi_resource *resource)
{
  uint64_t *address = user;
  if (resource->type == UACPI_RESOURCE_TYPE_FIXED_MEMORY32 &&
      resource->fixed_memory32.length >= UCSI_MAILBOX_BYTES) {
    *address = resource->fixed_memory32.address;
    return UACPI_ITERATION_DECISION_BREAK;
  }
  return UACPI_ITERATION_DECISION_CONTINUE;
}

static uacpi_iteration_decision add_device(void *user, uacpi_namespace_node *node,
                                           uacpi_u32 depth)
{
  (void)user;
  (void)depth;
  if (ucsi.node) {
    kinfo("ACPI: UCSI: additional %s device ignored\n", UCSI_ID);
    return UACPI_ITERATION_DECISION_CONTINUE;
  }
  ucsi.node = node;
  return UACPI_ITERATION_DECISION_CONTINUE;
}

void acpi_ucsi_start(void)
{
  uacpi_find_devices(UCSI_ID, add_device, UACPI_NULL);
  if (!ucsi.node) {
    return;
  }
  const char *path = uacpi_namespace_node_generate_absolute_path(ucsi.node);
  uint64_t address = 0;
  uacpi_status status = uacpi_for_each_device_resource(ucsi.node, "_CRS", find_mailbox,
                                                       &address);
  const char *missing = NULL;
  if (status != UACPI_STATUS_OK || !address) {
    missing = "no 32-byte fixed memory mailbox in _CRS";
  } else if (!(ucsi.mailbox = acpi_map(address, UCSI_MAILBOX_BYTES))) {
    missing = "mailbox mapping refused";
  } else if (!dsm_functions_present()) {
    missing = "_DSM write/refresh functions absent";
  } else if (uacpi_install_notify_handler(ucsi.node, ucsi_notify, UACPI_NULL) !=
             UACPI_STATUS_OK) {
    missing = "Notify registration failed";
  }
  if (missing) {
    kwarn("ACPI: UCSI %s: %s; client not started\n", path ? path : "?", missing);
  } else {
    kdebug("ACPI: UCSI %s: mailbox 0x%llx\n", path ? path : "?", (unsigned long long)address);
    ucsi.phase = UCSI_VERSION_WAIT;
    ucsi.deadline = arch_monotonic_ns() + UCSI_VERSION_NS;
  }
  if (path) {
    uacpi_free_absolute_path(path);
  }
}

uint64_t acpi_ucsi_poll(uint64_t now)
{
  bool notified = ucsi.notified;
  ucsi.notified = false;
  switch (ucsi.phase) {
  case UCSI_VERSION_WAIT:
    check_version(now);
    break;
  case UCSI_RESETTING:
  case UCSI_COMMAND:
  case UCSI_ACKNOWLEDGING:
    /* Firmware's Notify follows a mailbox refresh; otherwise poll it. */
    if (notified) {
      ucsi.cci = read_cci();
      advance();
    } else if (now >= ucsi.next_refresh) {
      ucsi.next_refresh = now + UCSI_REFRESH_NS;
      if (!refresh()) {
        park("CCI refresh failed");
      } else {
        advance();
      }
    }
    if ((ucsi.phase == UCSI_RESETTING || ucsi.phase == UCSI_COMMAND ||
         ucsi.phase == UCSI_ACKNOWLEDGING) && arch_monotonic_ns() >= ucsi.deadline) {
      park_expired();
    }
    break;
  case UCSI_IDLE:
    if (notified) {
      ucsi.cci = read_cci();
    }
    idle_step(now);
    break;
  default:
    break;
  }

  switch (ucsi.phase) {
  case UCSI_VERSION_WAIT:
    return ucsi.deadline;
  case UCSI_RESETTING:
  case UCSI_COMMAND:
  case UCSI_ACKNOWLEDGING:
    return ucsi.next_refresh < ucsi.deadline ? ucsi.next_refresh : ucsi.deadline;
  case UCSI_IDLE:
    return ucsi.idle_due;
  default:
    return UINT64_MAX;
  }
}
