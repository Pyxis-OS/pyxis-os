#ifndef BLUETOOTH_SETTINGS_H
#define BLUETOOTH_SETTINGS_H

#include <stdint.h>

/* Bounded storage and operation settings, not public ABI guarantees. */
#define HCI_COMMAND_CAPACITY 8
#define HCI_ACL_CAPACITY 8
#define HCI_RECEIVE_CAPACITY 32
#define HCI_CONNECTION_CAPACITY 8
#define HCI_OPERATION_NS UINT64_C(5000000000)
#define HCI_RECEIVE_PROGRESS_BUDGET 16

#endif
