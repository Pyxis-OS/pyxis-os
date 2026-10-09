#include <arch/clock.h>
#include <arch/cpu.h>
#include <arch/smp.h>
#include <kernel/bluetooth.h>
#include <kernel/log.h>
#include <kernel/memory.h>
#include <kernel/object/bluetooth_hci.h>
#include <kernel/panic.h>
#include "settings.h"
#include "firmware.h"
#include "../usb/host.h"

#define HCI_COMMAND_HEADER 3
#define HCI_EVENT_HEADER 2
#define HCI_ACL_HEADER 4
#define HCI_EVENT_MAX 257
#define HCI_HANDLE_MASK 0x0fff
#define HCI_HANDLE_MAX 0x0eff
#define HCI_ACL_PB_SHIFT 12
#define HCI_ACL_BC_MASK 0xc000
#define HCI_ACL_START_TX 0
#define HCI_ACL_CONTINUE 1
#define HCI_ACL_START_RX 2
#define HCI_EVENT_DISCONNECT 0x05
#define HCI_EVENT_BR_CONNECTION 0x03
#define HCI_EVENT_ENCRYPTION 0x08
#define HCI_EVENT_COMPLETE 0x0e
#define HCI_EVENT_STATUS 0x0f
#define HCI_EVENT_HARDWARE_ERROR 0x10
#define HCI_EVENT_COMPLETED_PACKETS 0x13
#define HCI_EVENT_BUFFER_OVERFLOW 0x1a
#define HCI_EVENT_KEY_REFRESH 0x30
#define HCI_EVENT_LE_META 0x3e
#define HCI_LE_CONNECTION 0x01
#define HCI_LE_ENHANCED_CONNECTION 0x0a
#define HCI_LE_CONNECTION_UPDATE 0x03
#define HCI_LE_REMOTE_FEATURES 0x04
#define HCI_LE_LONG_TERM_KEY 0x05
#define HCI_LE_CONNECTION_PARAMETERS 0x06
#define HCI_LE_DATA_LENGTH 0x07
#define HCI_LE_PHY_UPDATE 0x0c
#define HCI_LE_CHANNEL_SELECTION 0x14
#define HCI_OP_RESET 0x0c03
#define HCI_OP_EVENT_MASK 0x0c01
#define HCI_OP_LOCAL_VERSION 0x1001
#define HCI_OP_COMMANDS 0x1002
#define HCI_OP_FEATURES 0x1003
#define HCI_OP_BUFFER_SIZE 0x1005
#define HCI_OP_LE_EVENT_MASK 0x2001
#define HCI_OP_LE_BUFFER_SIZE 0x2002
#define HCI_OP_LE_FEATURES 0x2003
#define HCI_OP_EVENT_FILTER 0x0c05
#define HCI_OP_CONTROLLER_TO_HOST_FLOW 0x0c31
#define HCI_OP_HOST_BUFFER_SIZE 0x0c33
#define HCI_OP_HOST_COMPLETED_PACKETS 0x0c35
#define HCI_OP_EVENT_MASK_PAGE_2 0x0c63
#define HCI_OP_WRITE_FLOW_MODE 0x0c67
#define HCI_OP_WRITE_LE_HOST_SUPPORTED 0x0c6d
#define HCI_FEATURE_LE (UINT64_C(1) << 38)
#define HCI_LE_FEATURE_ENCRYPTION (UINT64_C(1) << 0)
#define HCI_LE_FEATURE_CONNECTION_PARAMETERS (UINT64_C(1) << 1)
#define HCI_LE_FEATURE_DATA_LENGTH (UINT64_C(1) << 5)
#define HCI_LE_FEATURE_PRIVACY (UINT64_C(1) << 6)
#define HCI_USB_COMMAND_REQUEST_TYPE 0x20
#define HCI_USB_COMMAND_REQUEST 0

struct hci_command {
  bool used, published, usb_done, hci_done, kernel, bulk, boot;
  uint16_t opcode;
  uint64_t deadline, id;
  size_t length;
  struct usb_ticket ticket;
  uint8_t wire[BLUETOOTH_HCI_COMMAND_MAX];
};

struct hci_acl {
  bool used, published, usb_done, controller_done;
  uint16_t handle;
  uint64_t generation, deadline, id;
  size_t length;
  struct usb_ticket ticket;
  uint8_t wire[BLUETOOTH_HCI_ACL_MAX];
};

struct hci_connection {
  bool live;
  uint16_t handle;
  uint32_t outstanding;
  uint64_t generation;
};

struct hci_record {
  struct bluetooth_hci_record metadata;
  uint8_t wire[BLUETOOTH_HCI_ACL_MAX];
};

struct hci_deferred_acl {
  struct hci_record record;
  uint64_t deadline;
};

struct hci_stream {
  uint64_t sequence, generation, epoch, deadline;
  size_t used, expected;
  uint8_t wire[BLUETOOTH_HCI_ACL_MAX];
};

enum hci_initialization {
  HCI_INIT_FIRMWARE,
  HCI_INIT_LOCAL_VERSION,
  HCI_INIT_COMMANDS,
  HCI_INIT_FEATURES,
  HCI_INIT_LE_FEATURES,
  HCI_INIT_LE_BUFFER,
  HCI_INIT_BR_BUFFER,
  HCI_INIT_EVENT_MASK,
  HCI_INIT_LE_EVENT_MASK,
  HCI_INIT_READY,
};

/* All shared state is BSP-owned with IF=0. USB calls require the owning worker
 * with IF=1; static entries remain reserved across those calls and preemption.
 * The core owns logical sessions; retained USB spans never belong to a process. */
static struct {
  struct usb_host_controller *host;
  struct usb_host_device *device;
  unsigned attachments;
  bool sealed, complete, terminal, dirty, cleanup, progress_active;
  enum call_status failure;
  enum hci_initialization initialization;
  const char *initialization_reason;
  bool initialization_hci_rejected;
  uint8_t initialization_hci_status;
  struct process *owner;
  uint64_t epoch_counter, epoch, receive_sequence, submission_counter, generation_counter;
  uint64_t features, le_features;
  uint32_t command_credits, acl_credits, acl_total, acl_packet_length;
  uint8_t supported_commands[64];
  bool firmware_started;
  struct bluetooth_firmware firmware;
  struct hci_command commands[HCI_COMMAND_CAPACITY];
  struct hci_acl acl[HCI_ACL_CAPACITY];
  struct hci_connection connections[HCI_CONNECTION_CAPACITY];
  uint8_t seen_handles[(HCI_HANDLE_MAX + 1 + 7) / 8];
  struct hci_record received[HCI_RECEIVE_CAPACITY];
  struct hci_deferred_acl deferred_acl[HCI_DEFERRED_ACL_CAPACITY];
  size_t receive_head, receive_count;
  size_t deferred_head, deferred_count;
  size_t request_count;
  struct hci_stream event_stream, boot_event_stream, acl_stream;
  uint8_t chunk[BLUETOOTH_HCI_ACL_MAX];
  struct bluetooth_hci_request *head, *tail, *reader;
} adapter;

static uint16_t read16(const uint8_t *wire)
{
  return wire[0] | ((uint16_t)wire[1] << 8);
}

static uint64_t read64(const uint8_t *wire)
{
  uint64_t value = 0;
  for (unsigned i = 0; i < 8; ++i) {
    value |= (uint64_t)wire[i] << (i * 8);
  }
  return value;
}

static void complete_request(struct bluetooth_hci_request *request, enum call_status result)
{
  request->result = result;
  request->process = NULL;
  request->request.next = NULL;
  bsp_request_complete(&request->request);
}

static const char *initialization_phase(void)
{
  if (!adapter.sealed || !adapter.complete || adapter.attachments != 1) {
    return "inventory";
  }
  switch (adapter.initialization) {
  case HCI_INIT_FIRMWARE: return bluetooth_firmware_phase_name(&adapter.firmware);
  case HCI_INIT_LOCAL_VERSION: return "local-version";
  case HCI_INIT_COMMANDS: return "supported-commands";
  case HCI_INIT_FEATURES: return "local-features";
  case HCI_INIT_LE_FEATURES: return "LE-features";
  case HCI_INIT_LE_BUFFER: return "LE-buffers";
  case HCI_INIT_BR_BUFFER: return "shared-buffers";
  case HCI_INIT_EVENT_MASK: return "event-mask";
  case HCI_INIT_LE_EVENT_MASK: return "LE-event-mask";
  case HCI_INIT_READY: return "runtime";
  }
  return "initialization";
}

static void fail_adapter_reason(enum call_status result, const char *reason)

{
  if (!adapter.terminal) {
    adapter.terminal = true;
    adapter.failure = result;
    adapter.receive_head = adapter.receive_count = 0;
    if (adapter.initialization_hci_rejected) {
      klog("Bluetooth HCI: unavailable: %s (phase %s, HCI status %u); reboot required\n",
          reason, initialization_phase(), adapter.initialization_hci_status);
    } else {
      klog("Bluetooth HCI: unavailable: %s (phase %s, call status %u); reboot required\n",
          reason, initialization_phase(), (unsigned)result);
    }
  }
  if (adapter.reader) {
    struct bluetooth_hci_request *reader = adapter.reader;
    adapter.reader = NULL;
    complete_request(reader, adapter.failure);
  }
  while (adapter.head) {
    struct bluetooth_hci_request *request = adapter.head;
    adapter.head = (void *)request->request.next;
    request->request.next = NULL;
    KASSERT(adapter.request_count);
    --adapter.request_count;
    if (!adapter.head) {
      adapter.tail = NULL;
    }
    complete_request(request, adapter.failure);
  }
}

static void fail_adapter(enum call_status result)
{
  const char *reason;
  switch (result) {
  case CALL_IO: reason = "USB or controller I/O failure"; break;
  case CALL_INPUT_LOST: reason = "receive continuity lost"; break;
  case CALL_TIMED_OUT: reason = "unpublished command deadline expired"; break;
  case CALL_OUTCOME_UNKNOWN: reason = "published command outcome unknown"; break;
  case CALL_LIMIT: reason = "sequence or clock limit exhausted"; break;
  default: reason = "controller operation failed"; break;
  }
  fail_adapter_reason(result, reason);
}

static struct hci_connection *find_connection(uint16_t handle)
{
  for (size_t i = 0; i < HCI_CONNECTION_CAPACITY; ++i) {
    if (adapter.connections[i].live && adapter.connections[i].handle == handle) {
      return &adapter.connections[i];
    }
  }
  return NULL;
}

static bool handle_seen(uint16_t handle)
{
  return handle <= HCI_HANDLE_MAX &&
      (adapter.seen_handles[handle / 8] & (1u << (handle % 8)));
}

static bool work_accounted(void)
{
  for (size_t i = 0; i < HCI_COMMAND_CAPACITY; ++i) {
    if (adapter.commands[i].used) {
      return false;
    }
  }
  for (size_t i = 0; i < HCI_ACL_CAPACITY; ++i) {
    if (adapter.acl[i].used) {
      return false;
    }
  }
  for (size_t i = 0; i < HCI_CONNECTION_CAPACITY; ++i) {
    if (adapter.connections[i].live || adapter.connections[i].outstanding) {
      return false;
    }
  }
  return !adapter.reader && !adapter.event_stream.used && !adapter.acl_stream.used &&
      !adapter.deferred_count;
}

static void end_session(void)
{
  adapter.owner = NULL;
  adapter.epoch = 0;
  adapter.receive_sequence = 0;
  adapter.receive_head = adapter.receive_count = 0;
  if (adapter.dirty || !work_accounted()) {
    fail_adapter_reason(CALL_UNAVAILABLE, "service cleanup unconfirmed");
  }
}

static bool read_only_command(uint16_t opcode)
{
  return opcode == HCI_OP_LOCAL_VERSION || opcode == HCI_OP_COMMANDS ||
      opcode == HCI_OP_FEATURES || opcode == HCI_OP_BUFFER_SIZE ||
      opcode == HCI_OP_LE_BUFFER_SIZE || opcode == HCI_OP_LE_FEATURES;
}

static bool command_allowed(uint16_t opcode)
{
  /* Reset, vendor commands, event masks and controller/host flow accounting
   * remain kernel-owned. This transport adds no runtime radio policy. */
  if ((opcode >> 10) == 0x3f || opcode == HCI_OP_RESET ||
      opcode == HCI_OP_EVENT_MASK || opcode == HCI_OP_LE_EVENT_MASK) {
    return false;
  }
  switch (opcode) {
  case HCI_OP_EVENT_FILTER:
  case HCI_OP_CONTROLLER_TO_HOST_FLOW:
  case HCI_OP_HOST_BUFFER_SIZE:
  case HCI_OP_HOST_COMPLETED_PACKETS:
  case HCI_OP_EVENT_MASK_PAGE_2:
  case HCI_OP_WRITE_FLOW_MODE:
  case HCI_OP_WRITE_LE_HOST_SUPPORTED:
    return false;
  default:
    return opcode != 0;
  }
}

static void reply_status(struct bluetooth_hci_request *request)
{
  uint32_t flags = 0;
  if (adapter.sealed && adapter.complete && adapter.attachments == 1 &&
      adapter.initialization == HCI_INIT_READY && !adapter.terminal) {
    flags |= BLUETOOTH_HCI_READY_DEVELOPMENT;
  }
  if (adapter.owner) {
    flags |= BLUETOOTH_HCI_OWNED;
  }
  if (adapter.terminal) {
    flags |= BLUETOOTH_HCI_TERMINAL;
  }
  request->data.status = (struct bluetooth_hci_status){
    .features = adapter.features, .le_features = adapter.le_features,
    .flags = flags, .command_max = BLUETOOTH_HCI_COMMAND_MAX,
    .acl_max = BLUETOOTH_HCI_ACL_MAX,
    .command_queue_capacity = HCI_COMMAND_CAPACITY, .acl_queue_capacity = HCI_ACL_CAPACITY,
    .receive_queue_capacity = HCI_RECEIVE_CAPACITY,
    .command_credits = adapter.command_credits, .acl_credits = adapter.acl_credits,
    .acl_packet_length = adapter.acl_packet_length,
  };
  complete_request(request, CALL_OK);
}

static void collect_record(struct bluetooth_hci_request *request)
{
  KASSERT(adapter.receive_count && !adapter.terminal);
  const struct hci_record *record = &adapter.received[adapter.receive_head];
  if (request->receive_capacity < record->metadata.length) {
    complete_request(request, CALL_BUFFER_TOO_SMALL);
    return;
  }
  request->data.received.record = record->metadata;
  memcpy(request->data.received.wire, record->wire, record->metadata.length);
  adapter.receive_head = (adapter.receive_head + 1) % HCI_RECEIVE_CAPACITY;
  --adapter.receive_count;
  complete_request(request, CALL_OK);
}

static void queue_record(uint32_t kind, const uint8_t *wire, size_t length,
    uint64_t generation, uint64_t submission_id)
{
  if (!adapter.owner || adapter.terminal) {
    return;
  }
  if (adapter.receive_count == HCI_RECEIVE_CAPACITY || adapter.receive_sequence == UINT64_MAX) {
    fail_adapter(CALL_INPUT_LOST);
    return;
  }
  size_t tail = (adapter.receive_head + adapter.receive_count) % HCI_RECEIVE_CAPACITY;
  struct hci_record *record = &adapter.received[tail];
  record->metadata = (struct bluetooth_hci_record){
    .kind = kind, .length = length, .epoch = adapter.epoch,
    .sequence = ++adapter.receive_sequence, .connection_generation = generation,
    .submission_id = submission_id,
  };
  memcpy(record->wire, wire, length);
  ++adapter.receive_count;
  if (adapter.reader) {
    struct bluetooth_hci_request *reader = adapter.reader;
    adapter.reader = NULL;
    collect_record(reader);
  }
}

static enum call_status admit_command(struct bluetooth_hci_request *request)
{
  size_t length = request->packet_length;
  if (length < HCI_COMMAND_HEADER || length > BLUETOOTH_HCI_COMMAND_MAX ||
      length != HCI_COMMAND_HEADER + (size_t)request->data.wire[2]) {
    return CALL_BAD_REQUEST;
  }
  uint16_t opcode = read16(request->data.wire);
  if (!command_allowed(opcode)) {
    return CALL_DENIED;
  }
  struct hci_command *free = NULL;
  for (size_t i = 0; i < HCI_COMMAND_CAPACITY; ++i) {
    struct hci_command *command = &adapter.commands[i];
    if (command->used && command->opcode == opcode) {
      return CALL_BUSY;
    }
    if (!command->used && !free) {
      free = command;
    }
  }
  if (!free) {
    return CALL_QUEUE_FULL;
  }
  if (adapter.submission_counter == UINT64_MAX) {
    return CALL_LIMIT;
  }
  *free = (struct hci_command){.used = true, .opcode = opcode, .length = length,
    .deadline = request->deadline_ns, .id = ++adapter.submission_counter};
  memcpy(free->wire, request->data.wire, length);
  request->data.submitted.submission_id = free->id;
  return CALL_OK;
}

static enum call_status admit_acl(struct bluetooth_hci_request *request)
{
  size_t length = request->packet_length;
  if (length < HCI_ACL_HEADER || length > BLUETOOTH_HCI_ACL_MAX ||
      length != HCI_ACL_HEADER + (size_t)read16(request->data.wire + 2) ||
      length - HCI_ACL_HEADER > adapter.acl_packet_length) {
    return CALL_BAD_REQUEST;
  }
  uint16_t flags = read16(request->data.wire);
  unsigned boundary = (flags >> HCI_ACL_PB_SHIFT) & 3;
  uint16_t handle = flags & HCI_HANDLE_MASK;
  if ((flags & HCI_ACL_BC_MASK) || handle > HCI_HANDLE_MAX ||
      (boundary != HCI_ACL_START_TX && boundary != HCI_ACL_CONTINUE)) {
    return CALL_BAD_REQUEST;
  }
  struct hci_connection *connection = find_connection(handle);
  if (!connection) {
    return CALL_NOT_FOUND;
  }
  struct hci_acl *free = NULL;
  for (size_t i = 0; i < HCI_ACL_CAPACITY; ++i) {
    if (!adapter.acl[i].used) {
      free = &adapter.acl[i];
      break;
    }
  }
  if (!free) {
    return CALL_QUEUE_FULL;
  }
  if (adapter.submission_counter == UINT64_MAX) {
    return CALL_LIMIT;
  }
  memset(free, 0, sizeof(*free));
  free->used = true;
  free->handle = handle;
  free->generation = connection->generation;
  free->deadline = request->deadline_ns;
  free->length = length;
  free->id = ++adapter.submission_counter;
  memcpy(free->wire, request->data.wire, length);
  request->data.submitted.submission_id = free->id;
  return CALL_OK;
}

static void execute_request(struct bluetooth_hci_request *request)
{
  uint64_t now = arch_monotonic_ns();
  if (!request->deadline_ns || request->deadline_ns == UINT64_MAX ||
      (request->deadline_ns > now && request->deadline_ns - now > HCI_OPERATION_NS)) {
    complete_request(request, CALL_BAD_REQUEST);
  } else if (adapter.terminal) {
    complete_request(request, adapter.failure);
  } else if (!adapter.sealed || !adapter.complete || adapter.attachments != 1 ||
      adapter.initialization != HCI_INIT_READY) {
    complete_request(request, CALL_UNAVAILABLE);
  } else if (request->operation == BLUETOOTH_HCI_ACQUIRE) {
    if (request->flags & ~BLUETOOTH_HCI_ACQUIRE_ALLOW_DEVELOPMENT) {
      complete_request(request, CALL_BAD_REQUEST);
    } else if (adapter.owner || adapter.cleanup) {
      complete_request(request, CALL_BUSY);
    } else if (!(request->flags & BLUETOOTH_HCI_ACQUIRE_ALLOW_DEVELOPMENT)) {
      complete_request(request, CALL_UNAVAILABLE);
    } else if (request->deadline_ns <= now) {
      complete_request(request, CALL_TIMED_OUT);
    } else if (adapter.epoch_counter == UINT64_MAX) {
      complete_request(request, CALL_LIMIT);
    } else {
      adapter.owner = request->process;
      adapter.epoch = ++adapter.epoch_counter;
      adapter.receive_sequence = 0;
      request->data.acquired.epoch = adapter.epoch;
      complete_request(request, CALL_OK);
    }
  } else if (request->process != adapter.owner || !request->epoch || request->epoch != adapter.epoch) {
    complete_request(request, CALL_DENIED);
  } else if (request->operation == BLUETOOTH_HCI_RELEASE) {
    if (request->deadline_ns <= now) {
      complete_request(request, CALL_TIMED_OUT);
    } else {
      end_session();
      complete_request(request, adapter.terminal ? CALL_UNAVAILABLE : CALL_OK);
    }
  } else if (request->operation == BLUETOOTH_HCI_RECEIVE) {
    if (adapter.receive_count) {
      collect_record(request);
    } else if (request->deadline_ns <= now) {
      complete_request(request, CALL_TIMED_OUT);
    } else {
      KASSERT(!adapter.reader);
      adapter.reader = request;
    }
  } else if (request->deadline_ns <= now) {
    complete_request(request, CALL_TIMED_OUT);
  } else if (request->operation == BLUETOOTH_HCI_SUBMIT_COMMAND) {
    complete_request(request, admit_command(request));
  } else if (request->operation == BLUETOOTH_HCI_SUBMIT_ACL) {
    complete_request(request, admit_acl(request));
  } else {
    complete_request(request, CALL_BAD_OPERATION);
  }
}

void bluetooth_hci_request_forward(struct bluetooth_hci_request *request)
{
  KASSERT(arch_cpu_index() == 0 && request->process &&
      request->request.state == BSP_REQUEST_FORWARDED);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  if (request->operation == BLUETOOTH_HCI_STATUS) {
    reply_status(request);
    return;
  }
  uint64_t now = arch_monotonic_ns();
  if (!request->deadline_ns || request->deadline_ns == UINT64_MAX ||
      (request->deadline_ns > now && request->deadline_ns - now > HCI_OPERATION_NS)) {
    complete_request(request, CALL_BAD_REQUEST);
    return;
  }
  if (!adapter.host || adapter.terminal) {
    complete_request(request, adapter.terminal ? adapter.failure : CALL_UNAVAILABLE);
    return;
  }
  if (adapter.request_count == HCI_REQUEST_CAPACITY) {
    complete_request(request, CALL_QUEUE_FULL);
    return;
  }
  KASSERT(!request->request.next);
  if (adapter.tail) {
    adapter.tail->request.next = &request->request;
  } else {
    adapter.head = request;
  }
  adapter.tail = request;
  ++adapter.request_count;
  usb_host_notify(adapter.host);
}

void bluetooth_hci_process_exit(struct process *process)
{
  KASSERT(arch_cpu_index() == 0);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  if (adapter.owner == process) {
    KASSERT(!adapter.reader);
    adapter.owner = NULL;
    adapter.epoch = 0;
    adapter.receive_head = adapter.receive_count = 0;
    adapter.cleanup = true;
    if (adapter.host) {
      usb_host_notify(adapter.host);
    }
  }
}

static size_t service_requests(size_t budget)
{
  size_t serviced = 0;
  while (serviced < budget && adapter.head) {
    struct bluetooth_hci_request *request = adapter.head;
    adapter.head = (void *)request->request.next;
    request->request.next = NULL;
    KASSERT(adapter.request_count);
    --adapter.request_count;
    if (!adapter.head) {
      adapter.tail = NULL;
    }
    ++serviced;
    execute_request(request);
  }
  return serviced;
}

static bool initialization_reply(const uint8_t *reply, size_t length)
{
  adapter.initialization_reason = "invalid initialization reply";
  if (!length) {
    adapter.initialization_reason = "empty initialization reply";
    return false;
  }
  if (reply[0]) {
    adapter.initialization_reason = "controller rejected initialization command";
    ktrace("Bluetooth HCI: initialization HCI status %u\n", reply[0]);
    return false;
  }
  switch (adapter.initialization) {
  case HCI_INIT_LOCAL_VERSION:
    if (length != 9 || reply[1] < 6 || read16(reply + 5) != 2) {
      return false;
    }
    ktrace("Bluetooth HCI: HCI version %u, revision %u\n", reply[1], read16(reply + 2));
    break;
  case HCI_INIT_COMMANDS:
    if (length != 65 || (reply[26] & 7) != 7) {
      return false;
    }
    memcpy(adapter.supported_commands, reply + 1, sizeof(adapter.supported_commands));
    break;
  case HCI_INIT_FEATURES:
    if (length != 9) {
      return false;
    }
    adapter.features = read64(reply + 1);
    if (!(adapter.features & HCI_FEATURE_LE)) {
      return false;
    }
    break;
  case HCI_INIT_LE_FEATURES:
    if (length != 9) {
      return false;
    }
    adapter.le_features = read64(reply + 1);
    break;
  case HCI_INIT_LE_BUFFER:
    if (length != 4) {
      return false;
    }
    adapter.acl_packet_length = read16(reply + 1);
    adapter.acl_total = reply[3];
    break;
  case HCI_INIT_BR_BUFFER:
    if (length != 8) {
      return false;
    }
    adapter.acl_packet_length = read16(reply + 1);
    adapter.acl_total = read16(reply + 4);
    break;
  case HCI_INIT_EVENT_MASK:
  case HCI_INIT_LE_EVENT_MASK:
    if (length != 1) {
      return false;
    }
    break;
  default:
    return false;
  }
  return true;
}

static void advance_initialization(void)
{
  ++adapter.initialization;
  if (adapter.initialization == HCI_INIT_BR_BUFFER && adapter.acl_total) {
    ++adapter.initialization;
  }
  if (adapter.initialization == HCI_INIT_EVENT_MASK) {
    if (adapter.acl_packet_length < 27 ||
        adapter.acl_packet_length > BLUETOOTH_HCI_ACL_MAX - HCI_ACL_HEADER || !adapter.acl_total) {
      fail_adapter_reason(CALL_UNAVAILABLE, "invalid controller ACL buffer limits");
      return;
    }
    adapter.acl_credits = adapter.acl_total;
  }
  if (adapter.initialization == HCI_INIT_READY) {
    klog("Bluetooth HCI: AX200 USB ready (%s, DDC, development firmware)\n",
        adapter.firmware.cold ? "cold upload" : "warm skip");
    ktrace("Bluetooth HCI: firmware %u/%u/%u, LE features %llx, ACL bytes %u credits %u\n",
        adapter.firmware.version[6], adapter.firmware.version[7],
        2000u + adapter.firmware.version[8], (unsigned long long)adapter.le_features,
        adapter.acl_packet_length, adapter.acl_total);
  }
}

static void retire_command(struct hci_command *command, bool boot_boundary)
{
  if (!command->used || !command->usb_done || !command->hci_done) {
    return;
  }
  bool kernel = command->kernel, boot = command->boot;
  if (boot && !boot_boundary) {
    return;
  }
  if (kernel && adapter.initialization == HCI_INIT_FIRMWARE && !adapter.terminal) {
    if (boot && (adapter.event_stream.used || adapter.boot_event_stream.used)) {
      fail_adapter_reason(CALL_INPUT_LOST, "partial event at firmware boot boundary");
      return;
    }
    if (!bluetooth_firmware_retired(&adapter.firmware, arch_monotonic_ns())) {
      fail_adapter_reason(CALL_UNAVAILABLE, bluetooth_firmware_reason(&adapter.firmware));
      return;
    }
    if (boot) {
      /* The confirmed firmware restart begins a fresh HCI startup window.
       * This is its initial allowance, not a synthesized completion event. */
      adapter.command_credits = 1;
      adapter.acl_stream.sequence = adapter.boot_event_stream.sequence;
    }
  } else if (kernel && !adapter.terminal) {
    advance_initialization();
  }
  memzero_explicit(command, sizeof(*command));
}

static bool command_event(const uint8_t *wire, size_t length, uint64_t *submission_id)
{
  bool complete = wire[0] == HCI_EVENT_COMPLETE;
  if ((complete && length < 5) || (!complete && length != 6)) {
    return false;
  }
  uint16_t opcode = read16(wire + (complete ? 3 : 4));
  adapter.command_credits = wire[complete ? 2 : 3];
  /* Opcode zero advertises allowance without completing a command. */
  if (!opcode) {
    return true;
  }
  struct hci_command *command = NULL;
  for (size_t i = 0; i < HCI_COMMAND_CAPACITY; ++i) {
    if (adapter.commands[i].used && adapter.commands[i].published &&
        adapter.commands[i].opcode == opcode) {
      command = &adapter.commands[i];
      break;
    }
  }
  if (!command || command->hci_done) {
    return false;
  }
  if (command->kernel) {
    adapter.initialization_hci_status = complete && length > 5 ? wire[5] :
        complete ? 0 : wire[2];
    adapter.initialization_hci_rejected = adapter.initialization_hci_status != 0;
    if (!complete || command->boot) {
      fail_adapter_reason(CALL_UNAVAILABLE, command->boot ?
          "unexpected completion for soft boot" : "initialization returned Command Status");
      return true;
    }
    bool previous_boot_events = adapter.firmware.bulk_events;
    bool valid = adapter.initialization == HCI_INIT_FIRMWARE ?
        bluetooth_firmware_reply(&adapter.firmware, opcode, wire + 5, length - 5,
            arch_monotonic_ns()) : initialization_reply(wire + 5, length - 5);
    if (!valid) {
      fail_adapter_reason(CALL_UNAVAILABLE, adapter.initialization == HCI_INIT_FIRMWARE ?
          bluetooth_firmware_reason(&adapter.firmware) : adapter.initialization_reason);
      return true;
    }
    if (!previous_boot_events && adapter.firmware.bulk_events) {
      if (adapter.acl_stream.used) {
        fail_adapter_reason(CALL_INPUT_LOST, "partial bulk frame entering bootloader mode");
        return true;
      }
      adapter.boot_event_stream.sequence = adapter.acl_stream.sequence;
    }
  } else {
    *submission_id = command->id;
  }
  command->hci_done = true;
  retire_command(command, false);
  return true;
}

static bool completed_packets(const uint8_t *wire, size_t length, uint64_t *generation)
{
  if (length < 3 || length != 3 + (size_t)wire[2] * 4) {
    return false;
  }
  /* Validate the entire event before applying any credit change. */
  uint32_t total = 0;
  for (unsigned i = 0; i < wire[2]; ++i) {
    uint16_t handle = read16(wire + 3 + i * 4);
    uint16_t count = read16(wire + 5 + i * 4);
    struct hci_connection *connection = find_connection(handle);
    if (!connection || count > connection->outstanding) {
      return false;
    }
    uint32_t tracked = 0;
    for (size_t j = 0; j < HCI_ACL_CAPACITY; ++j) {
      const struct hci_acl *packet = &adapter.acl[j];
      if (packet->used && packet->published && !packet->controller_done &&
          packet->generation == connection->generation) {
        ++tracked;
      }
    }
    if (count > tracked) {
      return false;
    }
    for (unsigned j = 0; j < i; ++j) {
      if (read16(wire + 3 + j * 4) == handle) {
        return false;
      }
    }
    total += count;
  }
  if (total > adapter.acl_total - adapter.acl_credits) {
    return false;
  }
  if (wire[2] == 1) {
    *generation = find_connection(read16(wire + 3))->generation;
  }
  for (unsigned i = 0; i < wire[2]; ++i) {
    struct hci_connection *connection = find_connection(read16(wire + 3 + i * 4));
    uint16_t count = read16(wire + 5 + i * 4);
    connection->outstanding -= count;
    for (unsigned j = 0; j < count; ++j) {
      struct hci_acl *oldest = NULL;
      for (size_t k = 0; k < HCI_ACL_CAPACITY; ++k) {
        struct hci_acl *packet = &adapter.acl[k];
        if (packet->used && packet->published && !packet->controller_done &&
            packet->generation == connection->generation &&
            (!oldest || packet->id < oldest->id)) {
          oldest = packet;
        }
      }
      KASSERT(oldest);
      oldest->controller_done = true;
      if (oldest->usb_done) {
        memzero_explicit(oldest, sizeof(*oldest));
      }
    }
  }
  adapter.acl_credits += total;
  return true;
}

static bool connection_event(const uint8_t *wire, size_t length, uint64_t *generation)
{
  size_t expected = wire[2] == HCI_LE_CONNECTION ? 21 : 33;
  if (length != expected) {
    return false;
  }
  if (wire[3]) {
    return true;
  }
  uint16_t handle = read16(wire + 4);
  if (handle > HCI_HANDLE_MAX || find_connection(handle) || !adapter.owner ||
      adapter.generation_counter == UINT64_MAX) {
    return false;
  }
  /* Independent endpoints have no qualified reuse drain boundary. Task 2
   * fails closed on handle reuse; connection/reconnect must establish it. */
  if (handle_seen(handle)) {
    return false;
  }
  for (size_t i = 0; i < HCI_CONNECTION_CAPACITY; ++i) {
    struct hci_connection *connection = &adapter.connections[i];
    if (!connection->live) {
      *connection = (struct hci_connection){.live = true, .handle = handle,
        .generation = ++adapter.generation_counter};
      adapter.dirty = true;
      adapter.seen_handles[handle / 8] |= 1u << (handle % 8);
      *generation = connection->generation;
      return true;
    }
  }
  return false;
}

static bool disconnect_event(const uint8_t *wire, size_t length, uint64_t *generation)
{
  if (length != 6) {
    return false;
  }
  struct hci_connection *connection = find_connection(read16(wire + 3));
  if (!connection) {
    return false;
  }
  *generation = connection->generation;
  if (!wire[2]) {
    /* Successful disconnect flushes the controller's remaining packets for
     * this handle. USB OUT tickets still need their independent completion. */
    if (connection->outstanding > adapter.acl_total - adapter.acl_credits) {
      return false;
    }
    adapter.acl_credits += connection->outstanding;
    connection->outstanding = 0;
    connection->live = false;
    for (size_t i = 0; i < HCI_ACL_CAPACITY; ++i) {
      struct hci_acl *packet = &adapter.acl[i];
      if (packet->used && packet->generation == *generation) {
        if (!packet->published) {
          /* An accepted packet cannot silently disappear on link loss. */
          fail_adapter(CALL_INPUT_LOST);
        } else {
          packet->controller_done = true;
          if (packet->usb_done) {
            memzero_explicit(packet, sizeof(*packet));
          }
        }
      }
    }
  }
  return true;
}

static bool event_generation(const uint8_t *wire, size_t length, uint64_t *generation)
{
  size_t handle_offset = 0, expected = 0;
  if (wire[0] == HCI_EVENT_ENCRYPTION) {
    handle_offset = 3;
    expected = 6;
  } else if (wire[0] == HCI_EVENT_KEY_REFRESH) {
    handle_offset = 3;
    expected = 5;
  } else if (wire[0] == HCI_EVENT_LE_META) {
    switch (wire[2]) {
    case HCI_LE_CONNECTION_UPDATE:
      expected = 12;
      handle_offset = 4;
      break;
    case HCI_LE_REMOTE_FEATURES:
      expected = 14;
      handle_offset = 4;
      break;
    case HCI_LE_PHY_UPDATE:
      handle_offset = 4;
      expected = 8;
      break;
    case HCI_LE_LONG_TERM_KEY:
      handle_offset = 3;
      expected = 15;
      break;
    case HCI_LE_CONNECTION_PARAMETERS:
    case HCI_LE_DATA_LENGTH:
      handle_offset = 3;
      expected = 13;
      break;
    case HCI_LE_CHANNEL_SELECTION:
      handle_offset = 3;
      expected = 6;
      break;
    default:
      break;
    }
  }
  if (!handle_offset) {
    return true;
  }
  if (length != expected) {
    return false;
  }
  struct hci_connection *connection = find_connection(read16(wire + handle_offset));
  if (!connection) {
    return false;
  }
  *generation = connection->generation;
  return true;
}

static void defer_acl(const uint8_t *wire, size_t length, uint64_t generation,
    uint64_t epoch, uint64_t deadline)
{
  if (adapter.deferred_count == HCI_DEFERRED_ACL_CAPACITY) {
    fail_adapter(CALL_INPUT_LOST);
    return;
  }
  size_t tail = (adapter.deferred_head + adapter.deferred_count) % HCI_DEFERRED_ACL_CAPACITY;
  struct hci_deferred_acl *deferred = &adapter.deferred_acl[tail];
  deferred->record.metadata = (struct bluetooth_hci_record){
    .kind = BLUETOOTH_HCI_RECORD_ACL, .length = length,
    .epoch = epoch, .connection_generation = generation,
  };
  memcpy(deferred->record.wire, wire, length);
  deferred->deadline = deadline;
  ++adapter.deferred_count;
}

static void flush_deferred_acl(void)
{
  for (size_t i = 0; i < HCI_DEFERRED_ACL_CAPACITY && adapter.deferred_count &&
       !adapter.terminal; ++i) {
    struct hci_deferred_acl *deferred = &adapter.deferred_acl[adapter.deferred_head];
    struct hci_record *record = &deferred->record;
    uint16_t handle = read16(record->wire) & HCI_HANDLE_MASK;
    if (!adapter.owner || record->metadata.epoch != adapter.epoch ||
        deferred->deadline <= arch_monotonic_ns()) {
      fail_adapter(CALL_INPUT_LOST);
      return;
    }
    struct hci_connection *connection = find_connection(handle);
    if (connection) {
      if (record->metadata.connection_generation &&
          record->metadata.connection_generation != connection->generation) {
        fail_adapter(CALL_INPUT_LOST);
        return;
      }
      queue_record(BLUETOOTH_HCI_RECORD_ACL, record->wire, record->metadata.length,
          connection->generation, 0);
      if (adapter.terminal) {
        return;
      }
    } else if (!handle_seen(handle)) {
      /* The bulk endpoint can finish before the first connection event.
       * Keep its whole frame and every later ACL frame in endpoint order. */
      return;
    }
    /* A seen, retired handle needs no delivery; Disconnect remains visible. */
    memzero_explicit(deferred, sizeof(*deferred));
    adapter.deferred_head = (adapter.deferred_head + 1) % HCI_DEFERRED_ACL_CAPACITY;
    --adapter.deferred_count;
  }
}

static void receive_event(const uint8_t *wire, size_t length)
{
  if (adapter.initialization == HCI_INIT_FIRMWARE && adapter.firmware_started) {
    if (!bluetooth_firmware_notify(&adapter.firmware, wire, length, arch_monotonic_ns())) {
      fail_adapter_reason(CALL_UNAVAILABLE, bluetooth_firmware_reason(&adapter.firmware));
      return;
    }
    struct hci_command *command = &adapter.commands[0];
    if (command->used && command->kernel && command->boot && command->published &&
        adapter.firmware.boot_notified) {
      command->hci_done = true;
    }
  }
  uint64_t generation = 0, submission_id = 0;
  bool valid = true;
  if (!wire[0]) {
    valid = false;
  } else if (wire[0] == HCI_EVENT_COMPLETE || wire[0] == HCI_EVENT_STATUS) {
    valid = command_event(wire, length, &submission_id);
  } else if (wire[0] == HCI_EVENT_COMPLETED_PACKETS) {
    valid = completed_packets(wire, length, &generation);
  } else if (wire[0] == HCI_EVENT_DISCONNECT) {
    valid = disconnect_event(wire, length, &generation);
  } else if (wire[0] == HCI_EVENT_HARDWARE_ERROR || wire[0] == HCI_EVENT_BUFFER_OVERFLOW) {
    fail_adapter(CALL_IO);
  } else if (wire[0] == HCI_EVENT_LE_META) {
    if (length < 3) {
      valid = false;
    } else if (wire[2] == HCI_LE_CONNECTION || wire[2] == HCI_LE_ENHANCED_CONNECTION) {
      valid = connection_event(wire, length, &generation);
    } else {
      valid = event_generation(wire, length, &generation);
    }
  } else if (wire[0] == HCI_EVENT_BR_CONNECTION) {
    valid = false;
  } else {
    valid = event_generation(wire, length, &generation);
  }
  if (!valid) {
    fail_adapter(CALL_INPUT_LOST);
  }
  queue_record(BLUETOOTH_HCI_RECORD_EVENT, wire, length, generation, submission_id);
}

static void receive_acl(const uint8_t *wire, size_t length, uint64_t generation,
    uint64_t epoch, uint64_t deadline)
{
  uint16_t flags = read16(wire);
  unsigned boundary = (flags >> HCI_ACL_PB_SHIFT) & 3;
  uint16_t handle = flags & HCI_HANDLE_MASK;
  if (handle > HCI_HANDLE_MAX || (flags & HCI_ACL_BC_MASK) ||
      (boundary != HCI_ACL_START_RX && boundary != HCI_ACL_CONTINUE)) {
    fail_adapter(CALL_INPUT_LOST);
    return;
  }
  struct hci_connection *connection = find_connection(handle);
  if (!connection && handle_seen(handle)) {
    /* Interrupt and bulk endpoints have independent completion order. A
     * valid old-link packet can arrive after its Disconnect notification. */
    return;
  }
  if (!adapter.owner || !epoch || epoch != adapter.epoch ||
      (connection && generation && connection->generation != generation)) {
    fail_adapter(CALL_INPUT_LOST);
    return;
  }
  if (connection && !generation) {
    generation = connection->generation;
  }
  if (!connection || adapter.deferred_count) {
    defer_acl(wire, length, generation, epoch, deadline);
    return;
  }
  queue_record(BLUETOOTH_HCI_RECORD_ACL, wire, length, connection->generation, 0);
}

static void consume_chunk(struct hci_stream *stream, bool acl,
    const struct usb_interrupt_completion *completion)
{
  if (stream->sequence == UINT64_MAX || completion->sequence != stream->sequence + 1) {
    fail_adapter(CALL_INPUT_LOST);
    return;
  }
  stream->sequence = completion->sequence;
  if (adapter.terminal) {
    return;
  }
  size_t offset = 0;
  size_t header = acl ? HCI_ACL_HEADER : HCI_EVENT_HEADER;
  while (offset < completion->bytes && !adapter.terminal) {
    if (acl && !stream->used) {
      uint64_t now = arch_monotonic_ns();
      if (now > UINT64_MAX - HCI_OPERATION_NS) {
        fail_adapter(CALL_LIMIT);
        return;
      }
      stream->deadline = now + HCI_OPERATION_NS;
      stream->epoch = adapter.epoch;
    }
    size_t target = stream->expected ? stream->expected : header;
    size_t bytes = target - stream->used;
    if (bytes > completion->bytes - offset) {
      bytes = completion->bytes - offset;
    }
    memcpy(stream->wire + stream->used, adapter.chunk + offset, bytes);
    stream->used += bytes;
    offset += bytes;
    if (!stream->expected && stream->used == header) {
      stream->expected = header + (acl ? (size_t)read16(stream->wire + 2) : stream->wire[1]);
      if (stream->expected > (acl ? BLUETOOTH_HCI_ACL_MAX : HCI_EVENT_MAX)) {
        fail_adapter(CALL_INPUT_LOST);
        return;
      }
      if (acl) {
        uint16_t handle = read16(stream->wire) & HCI_HANDLE_MASK;
        struct hci_connection *connection = find_connection(handle);
        if (handle > HCI_HANDLE_MAX) {
          fail_adapter(CALL_INPUT_LOST);
          return;
        }
        stream->generation = connection ? connection->generation : 0;
      }
    }
    if (stream->expected && stream->used == stream->expected) {
      if (acl) {
        receive_acl(stream->wire, stream->used, stream->generation, stream->epoch, stream->deadline);
      } else {
        receive_event(stream->wire, stream->used);
      }
      stream->used = stream->expected = 0;
      stream->generation = stream->epoch = stream->deadline = 0;
    }
  }
}

static enum call_status usb_failure(enum usb_result result, bool published)
{
  if (result == USB_TIMEOUT) {
    return published ? CALL_OUTCOME_UNKNOWN : CALL_TIMED_OUT;
  }
  if (result == USB_DISCONTINUITY || result == USB_STALE) {
    return CALL_INPUT_LOST;
  }
  return CALL_IO;
}

static void check_receive_deadline(void)
{
  if (adapter.reader && adapter.reader->deadline_ns <= arch_monotonic_ns()) {
    struct bluetooth_hci_request *reader = adapter.reader;
    adapter.reader = NULL;
    complete_request(reader, CALL_TIMED_OUT);
  }
}

static void check_deadlines(void)
{
  if (adapter.terminal) {
    return;
  }
  if (adapter.initialization == HCI_INIT_FIRMWARE && adapter.firmware_started) {
    bluetooth_firmware_tick(&adapter.firmware, arch_monotonic_ns());
    if (adapter.firmware.phase == BLUETOOTH_FIRMWARE_FAILED) {
      fail_adapter_reason(CALL_UNAVAILABLE, bluetooth_firmware_reason(&adapter.firmware));
      return;
    }
  }
  uint64_t now = arch_monotonic_ns();
  if ((adapter.acl_stream.used && adapter.acl_stream.deadline <= now) ||
      (adapter.deferred_count && adapter.deferred_acl[adapter.deferred_head].deadline <= now)) {
    fail_adapter(CALL_INPUT_LOST);
    return;
  }
  for (size_t i = 0; i < HCI_COMMAND_CAPACITY; ++i) {
    struct hci_command *command = &adapter.commands[i];
    if (command->used && command->deadline <= now) {
      fail_adapter(command->published ? CALL_OUTCOME_UNKNOWN : CALL_TIMED_OUT);
      return;
    }
  }
  for (size_t i = 0; i < HCI_ACL_CAPACITY; ++i) {
    struct hci_acl *packet = &adapter.acl[i];
    if (packet->used && packet->deadline <= now) {
      fail_adapter(packet->published ? CALL_OUTCOME_UNKNOWN : CALL_TIMED_OUT);
      return;
    }
  }
}

/* These collectors call USB only with IF=1, then restore the state guard before
 * interpreting its copied result. They never submit, wait, or allocate. */
static void collect_usb_commands(void)
{
  for (size_t i = 0; i < HCI_COMMAND_CAPACITY; ++i) {
    uint64_t flags = cpu_save_interrupts();
    struct hci_command *command = &adapter.commands[i];
    bool take = command->used && command->published && !command->usb_done;
    struct usb_ticket ticket = command->ticket;
    cpu_restore_interrupts(flags);
    if (!take || !ticket.generation) {
      continue;
    }
    struct usb_completion completion;
    enum usb_result result = command->bulk ?
        usb_host_async_bulk_out_take(adapter.device, ticket, &completion) :
        usb_host_control_take(adapter.device, ticket, NULL, 0, &completion);
    flags = cpu_save_interrupts();
    if (result == USB_OK) {
      command->usb_done = true;
      if (completion.result != USB_OK || completion.bytes != command->length) {
        fail_adapter(usb_failure(completion.result, true));
      }
      retire_command(command, false);
    } else if (result != USB_BUSY) {
      fail_adapter(usb_failure(result, true));
    }
    cpu_restore_interrupts(flags);
  }
}

static void collect_usb_acl(void)
{
  for (size_t i = 0; i < HCI_ACL_CAPACITY; ++i) {
    uint64_t flags = cpu_save_interrupts();
    struct hci_acl *packet = &adapter.acl[i];
    bool take = packet->used && packet->published && !packet->usb_done;
    struct usb_ticket ticket = packet->ticket;
    cpu_restore_interrupts(flags);
    if (!take || !ticket.generation) {
      continue;
    }
    struct usb_completion completion;
    enum usb_result result = usb_host_async_bulk_out_take(adapter.device, ticket, &completion);
    flags = cpu_save_interrupts();
    if (result == USB_OK) {
      if (completion.result != USB_OK || completion.bytes != packet->length) {
        fail_adapter(usb_failure(completion.result, true));
      }
      /* Controller credits belong to Number Of Completed Packets, never USB.
       * Keep the entry and its deadline until both sides are accounted for. */
      packet->usb_done = true;
      if (packet->controller_done) {
        memzero_explicit(packet, sizeof(*packet));
      }
    } else if (result != USB_BUSY) {
      fail_adapter(usb_failure(result, true));
    }
    cpu_restore_interrupts(flags);
  }
}

static void collect_receives(void)
{
  collect_usb_commands();
  collect_usb_acl();
  bool idle[2] = {false};
  for (unsigned stream_index = 0; stream_index < 2; ++stream_index) {
    for (unsigned i = 0; i < HCI_RECEIVE_PROGRESS_BUDGET; ++i) {
      struct usb_interrupt_completion completion;
      enum usb_result result = stream_index ?
          usb_host_async_bulk_take(adapter.device, adapter.chunk, sizeof(adapter.chunk), &completion) :
          usb_host_interrupt_take(adapter.device, adapter.chunk, sizeof(adapter.chunk), &completion);
      if (result == USB_BUSY) {
        idle[stream_index] = true;
        break;
      }
      uint64_t flags = cpu_save_interrupts();
      if (result != USB_OK || completion.bytes > sizeof(adapter.chunk)) {
        fail_adapter(usb_failure(result, false));
        cpu_restore_interrupts(flags);
        break;
      }
      /* Framing is fixed for this entire copied USB completion. */
      bool boot_events = stream_index && adapter.firmware.bulk_events;
      consume_chunk(stream_index ? (boot_events ? &adapter.boot_event_stream : &adapter.acl_stream) :
          &adapter.event_stream, stream_index && !boot_events, &completion);
      cpu_restore_interrupts(flags);
    }
    if (!stream_index) {
      uint64_t flags = cpu_save_interrupts();
      flush_deferred_acl();
      cpu_restore_interrupts(flags);
    }
  }
  if (idle[0] && idle[1]) {
    uint64_t flags = cpu_save_interrupts();
    retire_command(&adapter.commands[0], true);
    cpu_restore_interrupts(flags);
  }
}

static void prepare_initialization(void)
{
  if (!adapter.sealed || !adapter.complete || adapter.attachments != 1 ||
      adapter.terminal || adapter.initialization == HCI_INIT_READY) {
    return;
  }
  for (size_t i = 0; i < HCI_COMMAND_CAPACITY; ++i) {
    if (adapter.commands[i].used) {
      return;
    }
  }
  if (adapter.initialization == HCI_INIT_FIRMWARE) {
    if (!adapter.firmware_started) {
      bluetooth_firmware_init(&adapter.firmware, arch_monotonic_ns());
      adapter.firmware_started = true;
    }
    struct bluetooth_firmware_command operation;
    struct hci_command *command = &adapter.commands[0];
    enum bluetooth_firmware_progress progress = bluetooth_firmware_prepare(&adapter.firmware,
        command->wire, &operation, arch_monotonic_ns());
    if (progress == BLUETOOTH_FIRMWARE_ERROR) {
      fail_adapter_reason(CALL_UNAVAILABLE, bluetooth_firmware_reason(&adapter.firmware));
    } else if (progress == BLUETOOTH_FIRMWARE_DONE) {
      adapter.initialization = HCI_INIT_LOCAL_VERSION;
    } else if (progress == BLUETOOTH_FIRMWARE_COMMAND) {
      command->used = command->kernel = true;
      command->opcode = read16(command->wire);
      command->length = operation.length;
      command->deadline = operation.deadline;
      command->bulk = operation.route == BLUETOOTH_FIRMWARE_BULK;
      command->boot = operation.completion == BLUETOOTH_FIRMWARE_BOOT_NOTIFICATION;
    }
    return;
  }
  uint16_t opcode = 0;
  switch (adapter.initialization) {
  case HCI_INIT_LOCAL_VERSION:
    opcode = HCI_OP_LOCAL_VERSION;
    break;
  case HCI_INIT_COMMANDS:
    opcode = HCI_OP_COMMANDS;
    break;
  case HCI_INIT_FEATURES:
    opcode = HCI_OP_FEATURES;
    break;
  case HCI_INIT_LE_FEATURES:
    opcode = HCI_OP_LE_FEATURES;
    break;
  case HCI_INIT_LE_BUFFER:
    opcode = HCI_OP_LE_BUFFER_SIZE;
    break;
  case HCI_INIT_BR_BUFFER:
    opcode = HCI_OP_BUFFER_SIZE;
    break;
  case HCI_INIT_EVENT_MASK:
    opcode = HCI_OP_EVENT_MASK;
    break;
  case HCI_INIT_LE_EVENT_MASK:
    opcode = HCI_OP_LE_EVENT_MASK;
    break;
  default:
    KASSERT(false);
  }
  uint64_t now = arch_monotonic_ns();
  if (now > UINT64_MAX - HCI_OPERATION_NS) {
    fail_adapter(CALL_LIMIT);
    return;
  }
  struct hci_command *command = &adapter.commands[0];
  *command = (struct hci_command){.used = true, .kernel = true, .opcode = opcode,
    .length = HCI_COMMAND_HEADER, .deadline = now + HCI_OPERATION_NS};
  command->wire[0] = opcode;
  command->wire[1] = opcode >> 8;
  if (opcode == HCI_OP_EVENT_MASK) {
    /* Disconnection, command responses, hardware/overflow, credit and LE events
     * are mandatory; encryption change/key refresh are visible to the owner. */
    uint64_t mask = (UINT64_C(1) << (HCI_EVENT_DISCONNECT - 1)) |
        (UINT64_C(1) << (HCI_EVENT_ENCRYPTION - 1)) |
        (UINT64_C(1) << (HCI_EVENT_COMPLETE - 1)) |
        (UINT64_C(1) << (HCI_EVENT_STATUS - 1)) |
        (UINT64_C(1) << (HCI_EVENT_HARDWARE_ERROR - 1)) |
        (UINT64_C(1) << (HCI_EVENT_COMPLETED_PACKETS - 1)) |
        (UINT64_C(1) << (HCI_EVENT_BUFFER_OVERFLOW - 1)) |
        (UINT64_C(1) << (HCI_EVENT_KEY_REFRESH - 1)) |
        (UINT64_C(1) << (HCI_EVENT_LE_META - 1));
    command->length += 8;
    command->wire[2] = 8;
    for (unsigned i = 0; i < 8; ++i) {
      command->wire[3 + i] = mask >> (8 * i);
    }
  } else if (opcode == HCI_OP_LE_EVENT_MASK) {
    uint64_t mask = UINT64_C(0x0f); /* Connection, advertising, update, features. */
    if (adapter.le_features & HCI_LE_FEATURE_ENCRYPTION) {
      mask |= UINT64_C(1) << 4;
    }
    if (adapter.le_features & HCI_LE_FEATURE_CONNECTION_PARAMETERS) {
      mask |= UINT64_C(1) << 5;
    }
    if (adapter.le_features & HCI_LE_FEATURE_DATA_LENGTH) {
      mask |= UINT64_C(1) << 6;
    }
    if (adapter.le_features & HCI_LE_FEATURE_PRIVACY) {
      mask |= UINT64_C(1) << 9;
    }
    command->length += 8;
    command->wire[2] = 8;
    for (unsigned i = 0; i < 8; ++i) {
      command->wire[3 + i] = mask >> (8 * i);
    }
  }
}

static void publish_command(void)
{
  uint64_t flags = cpu_save_interrupts();
  if (adapter.terminal || adapter.cleanup || !adapter.command_credits) {
    cpu_restore_interrupts(flags);
    return;
  }
  struct hci_command *selected = NULL;
  for (size_t i = 0; i < HCI_COMMAND_CAPACITY; ++i) {
    struct hci_command *command = &adapter.commands[i];
    if (command->used && !command->published && (!selected || command->id < selected->id)) {
      selected = command;
    }
  }
  if (!selected) {
    cpu_restore_interrupts(flags);
    return;
  }
  selected->published = true;
  --adapter.command_credits;
  struct usb_setup setup = {
    .request_type = HCI_USB_COMMAND_REQUEST_TYPE, .request = HCI_USB_COMMAND_REQUEST,
    .length = selected->length,
  };
  cpu_restore_interrupts(flags);
  struct usb_ticket ticket;
  enum usb_result result = selected->bulk ?
      usb_host_async_bulk_out_submit(adapter.device, selected->wire, selected->length,
          selected->deadline, &ticket) :
      usb_host_control_submit(adapter.device, &setup, selected->wire, selected->deadline, &ticket);
  flags = cpu_save_interrupts();
  if (result == USB_OK) {
    selected->ticket = ticket;
    if (selected->kernel && adapter.initialization == HCI_INIT_FIRMWARE &&
        !bluetooth_firmware_published(&adapter.firmware, arch_monotonic_ns())) {
      fail_adapter_reason(CALL_UNAVAILABLE, bluetooth_firmware_reason(&adapter.firmware));
    }
    if (!selected->kernel && !read_only_command(selected->opcode)) {
      adapter.dirty = true;
    }
  } else {
    selected->published = false;
    ++adapter.command_credits;
    if (result != USB_BUSY) {
      fail_adapter(usb_failure(result, false));
    }
  }
  cpu_restore_interrupts(flags);
}

static void publish_acl(void)
{
  uint64_t flags = cpu_save_interrupts();
  if (adapter.terminal || adapter.cleanup || !adapter.acl_credits) {
    cpu_restore_interrupts(flags);
    return;
  }
  struct hci_acl *selected = NULL;
  for (size_t i = 0; i < HCI_ACL_CAPACITY; ++i) {
    struct hci_acl *packet = &adapter.acl[i];
    if (packet->used && !packet->published && (!selected || packet->id < selected->id)) {
      selected = packet;
    }
  }
  if (!selected) {
    cpu_restore_interrupts(flags);
    return;
  }
  struct hci_connection *connection = find_connection(selected->handle);
  if (!connection || connection->generation != selected->generation) {
    fail_adapter(CALL_INPUT_LOST);
    cpu_restore_interrupts(flags);
    return;
  }
  selected->published = true;
  --adapter.acl_credits;
  ++connection->outstanding;
  cpu_restore_interrupts(flags);
  struct usb_ticket ticket;
  enum usb_result result = usb_host_async_bulk_out_submit(adapter.device, selected->wire,
      selected->length, selected->deadline, &ticket);
  flags = cpu_save_interrupts();
  if (result == USB_OK) {
    selected->ticket = ticket;
    adapter.dirty = true;
  } else {
    selected->published = false;
    ++adapter.acl_credits;
    --connection->outstanding;
    if (result != USB_BUSY) {
      fail_adapter(usb_failure(result, false));
    }
  }
  cpu_restore_interrupts(flags);
}

static void work_tick(struct usb_host_controller *host)
{
  KASSERT(arch_cpu_index() == 0);
  uint64_t flags = cpu_save_interrupts();
  KASSERT(flags & RFLAGS_INTERRUPT_ENABLE);
  if (host != adapter.host || !adapter.device || adapter.progress_active) {
    cpu_restore_interrupts(flags);
    return;
  }
  adapter.progress_active = true;
  if (adapter.cleanup) {
    adapter.cleanup = false;
    end_session();
  }
  check_deadlines();
  size_t serviced = service_requests(HCI_REQUEST_CAPACITY);
  cpu_restore_interrupts(flags);
  collect_receives();
  flags = cpu_save_interrupts();
  if (adapter.cleanup) {
    adapter.cleanup = false;
    end_session();
  }
  flush_deferred_acl();
  /* A resumed AP can publish its next copied call while collection runs.
   * Consume it within the same fixed request budget before ending this tick. */
  service_requests(HCI_REQUEST_CAPACITY - serviced);
  check_deadlines();
  check_receive_deadline();
  prepare_initialization();
  cpu_restore_interrupts(flags);
  publish_command();
  publish_acl();
  flags = cpu_save_interrupts();
  adapter.progress_active = false;
  cpu_restore_interrupts(flags);
}

void bluetooth_hci_progress(struct usb_host_controller *host)
{
  work_tick(host);
}

void bluetooth_hci_drain_progress(struct usb_host_controller *host)
{
  work_tick(host);
}

void bluetooth_hci_candidate(void)
{
  KASSERT(arch_cpu_index() == 0);
  uint64_t flags = cpu_save_interrupts();
  KASSERT(flags & RFLAGS_INTERRUPT_ENABLE);
  ++adapter.attachments;
  if (adapter.attachments != 1 || adapter.sealed) {
    fail_adapter_reason(CALL_UNAVAILABLE, "multiple or late AX200 candidates");
  }
  cpu_restore_interrupts(flags);
}

void bluetooth_hci_attach(struct usb_host_controller *host,
    struct usb_host_device *device, uint8_t interface_number)
{
  KASSERT(arch_cpu_index() == 0 && host && device);
  uint64_t flags = cpu_save_interrupts();
  KASSERT(flags & RFLAGS_INTERRUPT_ENABLE);
  if (interface_number || adapter.attachments != 1 || adapter.host || adapter.sealed) {
    fail_adapter_reason(CALL_UNAVAILABLE, "invalid AX200 attachment");
  } else {
    adapter.host = host;
    adapter.device = device;
    adapter.command_credits = 1; /* Initial HCI command allowance before replies. */
  }
  cpu_restore_interrupts(flags);
}

void bluetooth_hci_inventory_sealed(bool complete)
{
  KASSERT(arch_cpu_index() == 0 && !adapter.sealed);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  adapter.sealed = true;
  adapter.complete = complete;
  if (!complete) {
    fail_adapter_reason(CALL_UNAVAILABLE, "USB inventory incomplete");
  } else if (adapter.attachments > 1) {
    fail_adapter_reason(CALL_UNAVAILABLE, "multiple AX200 candidates");
  }
  if (adapter.host) {
    usb_host_notify(adapter.host);
  }
}

void bluetooth_hci_transport_failed(struct usb_host_controller *host)
{
  KASSERT(arch_cpu_index() == 0);
  uint64_t flags = cpu_save_interrupts();
  if (adapter.host == host) {
    fail_adapter(CALL_IO);
  }
  cpu_restore_interrupts(flags);
}
