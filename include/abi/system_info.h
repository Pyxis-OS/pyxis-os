#ifndef ABI_SYSTEM_INFO_H
#define ABI_SYSTEM_INFO_H

#include <abi/message.h>

#define SYSTEM_INFO_RIGHT_READ (UINT64_C(1) << 0)
#define SYSTEM_INFO_IDENTITY UINT64_C(1)
#define SYSTEM_INFO_CPU UINT64_C(2)
#define SYSTEM_INFO_MEMORY UINT64_C(3)
#define SYSTEM_INFO_PCI UINT64_C(4)
#define SYSTEM_INFO_PCI_FUNCTION UINT64_C(5)

#define SYSTEM_INFO_PCI_UNAVAILABLE UINT64_C(1)
#define SYSTEM_INFO_PCI_INCOMPLETE UINT64_C(2)
#define SYSTEM_INFO_PCI_COMPLETE UINT64_C(3)

/* Synchronous queries on explicitly delegated system_info authority. All
 * require READ and send only a message_header, except PCI_FUNCTION.
 * Strings are NUL-terminated and unused bytes are zero.
 * Empty build_revision or brand means that field is unavailable; other fields
 * remain valid. No kernel pointers, physical map or ambient query is exposed. */
struct system_info_identity {
  char os_name[32];
  char kernel_name[32];
  char architecture[16];
  char build_revision[48]; /* Running kernel source commit, not SDK/userland. */
};

struct system_info_cpu {
  uint64_t online_count; /* Online logical CPUs, not cores or caller affinity. */
  char brand[56]; /* Guest-visible BSP sample; not a heterogeneous inventory. */
};

/* Coherent allocator capacity, excluding permanent reservations, not installed
 * RAM, available memory or RSS. total_bytes = allocated_bytes + free_bytes.
 * Label this observation Memory (allocator). Separate queries are not atomic. */
struct system_info_memory {
  uint64_t total_bytes;
  uint64_t allocated_bytes;
  uint64_t free_bytes;
};

/* The boot PCI inventory, fixed before user tasks start; there is no hotplug.
 * UNAVAILABLE means no supported configuration access (count is zero).
 * INCOMPLETE means malformed topology, unsupported headers or an unretained
 * function; the listed functions remain valid. Count is the retained number. */
struct system_info_pci {
  uint64_t state;
  uint64_t function_count;
};

/* Index 0..function_count-1 selects a retained function; later indices fail
 * NOT_FOUND. Order is stable for the boot but otherwise unspecified. */
struct system_info_pci_function_request {
  struct message_header header;
  uint64_t index;
};

/* Identity read at discovery; no configuration access, resources or driver
 * state. Header type excludes the multifunction bit. Reserved is zero. */
struct system_info_pci_function {
  uint16_t segment;
  uint8_t bus;
  uint8_t device;
  uint8_t function;
  uint8_t header_type;
  uint16_t vendor_id;
  uint16_t device_id;
  uint8_t base_class;
  uint8_t subclass;
  uint8_t interface;
  uint8_t revision;
  uint16_t reserved;
};

_Static_assert(sizeof(struct system_info_identity) == 128, "system identity layout");
_Static_assert(sizeof(struct system_info_cpu) == 64, "system CPU layout");
_Static_assert(sizeof(struct system_info_memory) == 24, "system memory layout");
_Static_assert(sizeof(struct system_info_pci) == 16, "system PCI layout");
_Static_assert(sizeof(struct system_info_pci_function_request) == 24, "PCI function request layout");
_Static_assert(sizeof(struct system_info_pci_function) == 16, "PCI function layout");

#endif
