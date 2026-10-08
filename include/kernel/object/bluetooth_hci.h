#ifndef KERNEL_OBJECT_BLUETOOTH_HCI_H
#define KERNEL_OBJECT_BLUETOOTH_HCI_H

#include <abi/bluetooth_hci.h>
#include <abi/syscall.h>
#include <kernel/object/object.h>
#include <kernel/service/request.h>

struct process;

struct bluetooth_hci_request {
  struct bsp_request request;
  /* Borrowed only while the uninterruptible caller owns its process. The core
   * checks process and epoch and clears this loan before completing. */
  struct process *process;
  uint64_t operation, epoch, deadline_ns, flags;
  size_t packet_length, receive_capacity;
  enum call_status result;
  union {
    struct bluetooth_hci_status status;
    struct bluetooth_hci_acquire_reply acquired;
    struct bluetooth_hci_submit_reply submitted;
    struct {
      struct bluetooth_hci_record record;
      uint8_t wire[BLUETOOTH_HCI_ACL_MAX];
    } received;
    uint8_t wire[BLUETOOTH_HCI_ACL_MAX];
  } data;
};

/* BSP/IF=0. Stateless authority wrapper; destruction never ends ownership. */
struct kernel_object *bluetooth_hci_create(void);
/* Current user task/IF=0. Validate and copy before lending request to BSP. */
struct syscall_result bluetooth_hci_call(uint64_t rights, uint64_t operation,
    uintptr_t request_address, size_t request_size, uintptr_t reply_address,
    size_t reply_capacity);

/* Physical core, BSP/IF=0. FORWARDED request ownership ends at completion;
 * process exit removes the owner before its allocation can be freed. */
void bluetooth_hci_request_forward(struct bluetooth_hci_request *request);
void bluetooth_hci_process_exit(struct process *process);

#endif
