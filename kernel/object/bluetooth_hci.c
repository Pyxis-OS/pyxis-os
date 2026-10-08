#include <arch/smp.h>
#include <kernel/mm/heap.h>
#include <kernel/object/bluetooth_hci.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/user_memory.h>

static void destroy_bluetooth_hci(struct kernel_object *object)
{
  kfree(object);
}

struct kernel_object *bluetooth_hci_create(void)
{
  KASSERT(arch_cpu_index() == 0);
  struct kernel_object *object = kmalloc(sizeof(*object));
  if (object) {
    object_init(object, OBJECT_BLUETOOTH_HCI, destroy_bluetooth_hci);
  }
  return object;
}

struct syscall_result bluetooth_hci_call(uint64_t rights, uint64_t operation,
    uintptr_t request_address, size_t request_size, uintptr_t reply_address,
    size_t reply_capacity)
{
  if (operation < BLUETOOTH_HCI_STATUS || operation > BLUETOOTH_HCI_RECEIVE) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if (!(rights & BLUETOOTH_HCI_RIGHT_CONTROL)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }

  bool submit = operation == BLUETOOTH_HCI_SUBMIT_COMMAND ||
      operation == BLUETOOTH_HCI_SUBMIT_ACL;
  size_t prefix_size = operation == BLUETOOTH_HCI_STATUS ? 0 : 2 * sizeof(uint64_t);
  size_t packet_length = 0;
  if (submit) {
    size_t maximum = operation == BLUETOOTH_HCI_SUBMIT_COMMAND ?
        BLUETOOTH_HCI_COMMAND_MAX : BLUETOOTH_HCI_ACL_MAX;
    size_t minimum = operation == BLUETOOTH_HCI_SUBMIT_COMMAND ? 3 : 4;
    if (request_size < prefix_size + minimum || request_size > prefix_size + maximum) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    packet_length = request_size - prefix_size;
  } else if (request_size != prefix_size) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }

  size_t reply_size = 0;
  if (operation == BLUETOOTH_HCI_STATUS) {
    reply_size = sizeof(struct bluetooth_hci_status);
  } else if (operation == BLUETOOTH_HCI_ACQUIRE) {
    reply_size = sizeof(struct bluetooth_hci_acquire_reply);
  } else if (submit) {
    reply_size = sizeof(struct bluetooth_hci_submit_reply);
  } else if (operation == BLUETOOTH_HCI_RECEIVE) {
    if (reply_capacity < sizeof(struct bluetooth_hci_record)) {
      return (struct syscall_result){CALL_BUFFER_TOO_SMALL, 0};
    }
    size_t wire_capacity = reply_capacity - sizeof(struct bluetooth_hci_record);
    if (wire_capacity > BLUETOOTH_HCI_ACL_MAX) {
      wire_capacity = BLUETOOTH_HCI_ACL_MAX;
    }
    reply_size = sizeof(struct bluetooth_hci_record) + wire_capacity;
  }
  if (reply_capacity < reply_size) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if ((request_size && !user_buffer_check(request_address, request_size, USER_BUFFER_READ)) ||
      (reply_size && !user_buffer_check(reply_address, reply_size, USER_BUFFER_WRITE))) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }

  uint64_t prefix[2] = {0};
  if (prefix_size) {
    KASSERT(copy_from_user(prefix, request_address, prefix_size));
    if (!prefix[1] || prefix[1] == UINT64_MAX) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
  }
  if (operation == BLUETOOTH_HCI_ACQUIRE &&
      (prefix[0] & ~BLUETOOTH_HCI_ACQUIRE_ALLOW_DEVELOPMENT)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (submit) {
    uint8_t wire_header[4];
    size_t header_size = operation == BLUETOOTH_HCI_SUBMIT_COMMAND ? 3 : 4;
    KASSERT(copy_from_user(wire_header, request_address + prefix_size, header_size));
    size_t payload_length = operation == BLUETOOTH_HCI_SUBMIT_COMMAND ? wire_header[2] :
        (size_t)wire_header[2] | ((size_t)wire_header[3] << 8);
    if (packet_length != header_size + payload_length) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
  }

  struct bluetooth_hci_request *request =
      (void *)bsp_request_prepare(BSP_SERVICE_BLUETOOTH_HCI);
  request->process = process_current();
  request->operation = operation;
  request->deadline_ns = prefix[1];
  if (operation == BLUETOOTH_HCI_ACQUIRE) {
    request->flags = prefix[0];
  } else {
    request->epoch = prefix[0];
  }
  request->packet_length = packet_length;
  if (submit) {
    KASSERT(copy_from_user(request->data.wire, request_address + prefix_size, packet_length));
  }
  if (operation == BLUETOOTH_HCI_RECEIVE) {
    request->receive_capacity = reply_size - sizeof(struct bluetooth_hci_record);
  }
  bsp_request_submit_and_wait(&request->request);
  KASSERT(!request->process);
  enum call_status status = request->result;
  if (status == CALL_OK) {
    const void *reply = NULL;
    if (operation == BLUETOOTH_HCI_STATUS) {
      reply = &request->data.status;
    } else if (operation == BLUETOOTH_HCI_ACQUIRE) {
      reply = &request->data.acquired;
    } else if (submit) {
      reply = &request->data.submitted;
    } else if (operation == BLUETOOTH_HCI_RECEIVE) {
      KASSERT(request->data.received.record.length <= request->receive_capacity);
      reply_size = sizeof(struct bluetooth_hci_record) + request->data.received.record.length;
      reply = &request->data.received;
    }
    if (reply_size) {
      KASSERT(reply && copy_to_user(reply_address, reply, reply_size));
    }
  } else {
    reply_size = 0;
  }
  bsp_request_release(&request->request);
  return (struct syscall_result){status, reply_size};
}
