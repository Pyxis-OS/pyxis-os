#ifndef ABI_SYSTEM_INFO_H
#define ABI_SYSTEM_INFO_H

#include <abi/message.h>

#define SYSTEM_INFO_RIGHT_READ (UINT64_C(1) << 0)
#define SYSTEM_INFO_IDENTITY UINT64_C(1)
#define SYSTEM_INFO_CPU UINT64_C(2)
#define SYSTEM_INFO_MEMORY UINT64_C(3)
#define SYSTEM_INFO_PCI UINT64_C(4)
#define SYSTEM_INFO_PCI_FUNCTION UINT64_C(5)
#define SYSTEM_INFO_USB UINT64_C(6)
#define SYSTEM_INFO_USB_CONTROLLER UINT64_C(7)
#define SYSTEM_INFO_USB_DEVICE UINT64_C(8)
#define SYSTEM_INFO_USB_INTERFACE UINT64_C(9)

#define SYSTEM_INFO_USB_UNAVAILABLE UINT64_C(1)
#define SYSTEM_INFO_USB_INITIALIZING UINT64_C(2)
#define SYSTEM_INFO_USB_INCOMPLETE UINT64_C(3)
#define SYSTEM_INFO_USB_COMPLETE UINT64_C(4)
#define SYSTEM_INFO_USB_CONTROLLER_UNSUPPORTED 1u
#define SYSTEM_INFO_USB_CONTROLLER_FAILED 2u
#define SYSTEM_INFO_USB_CONTROLLER_INCOMPLETE 3u
#define SYSTEM_INFO_USB_CONTROLLER_COMPLETE 4u
#define SYSTEM_INFO_USB_DEVICE_IDENTIFIED 1u
#define SYSTEM_INFO_USB_DEVICE_INCOMPLETE 2u
#define SYSTEM_INFO_USB_DEVICE_HUB 4u
#define SYSTEM_INFO_USB_SPEED_UNKNOWN 0u
#define SYSTEM_INFO_USB_SPEED_LOW 1u
#define SYSTEM_INFO_USB_SPEED_FULL 2u
#define SYSTEM_INFO_USB_SPEED_HIGH 3u
#define SYSTEM_INFO_USB_SPEED_SUPER 4u

#define SYSTEM_INFO_PCI_UNAVAILABLE UINT64_C(1)
#define SYSTEM_INFO_PCI_INCOMPLETE UINT64_C(2)
#define SYSTEM_INFO_PCI_COMPLETE UINT64_C(3)

/* Synchronous queries on explicitly delegated system_info authority. All
 * require READ and send only a message_header, except indexed PCI/USB queries.
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

/* Immutable boot snapshot, published after every supported controller finishes.
 * INITIALIZING/UNAVAILABLE have zero counts; indexed reads then fail NOT_FOUND.
 * INCOMPLETE retains observations but omits unsupported controllers' devices,
 * hub descendants and facts that could not be read within resource/deadline limits.
 * This is not a live view: later removal or failure does not change records. */
struct system_info_usb {
  uint64_t state;
  uint64_t controller_count;
  uint64_t device_count;
  uint64_t interface_count;
};

/* Indices use the corresponding count above and are stable for the boot.
 * Their order is otherwise unspecified. */
struct system_info_usb_request {
  struct message_header header;
  uint64_t index;
};

struct system_info_usb_controller {
  struct system_info_pci_function pci;
  uint32_t state;
  uint32_t root_port_count; /* Zero means unknown/not inspected, not zero ports. */
};

#define SYSTEM_INFO_USB_NO_PARENT UINT64_MAX

/* Unidentified connected ports have zero descriptor fields and IDENTIFIED clear.
 * root_port is the controller's one-based physical port, not a Linux bus/address.
 * Direct devices have NO_PARENT and parent_port zero. Descendants name an
 * earlier hub record on the same controller/root port and its one-based port.
 * interface_first/count select the contiguous validated interface records. */
struct system_info_usb_device {
  uint64_t controller_index;
  uint64_t interface_first;
  uint64_t interface_count;
  uint64_t parent_index;
  uint16_t root_port;
  uint16_t vendor_id;
  uint16_t product_id;
  uint16_t parent_port;
  uint8_t speed;
  uint8_t device_class;
  uint8_t device_subclass;
  uint8_t device_protocol;
  uint8_t configuration_count;
  uint8_t flags;
  uint16_t reserved;
};

/* Each advertised configuration and alternate setting is described; no claim
 * that it is selected or bound. Class/vendor descriptors remain opaque. */
struct system_info_usb_interface {
  uint64_t device_index;
  uint8_t configuration;
  uint8_t number;
  uint8_t alternate;
  uint8_t class;
  uint8_t subclass;
  uint8_t protocol;
  uint8_t endpoint_count;
  uint8_t reserved;
};

_Static_assert(sizeof(struct system_info_usb) == 32, "USB inventory layout");
_Static_assert(sizeof(struct system_info_usb_request) == 24, "USB index request layout");
_Static_assert(sizeof(struct system_info_usb_controller) == 24, "USB controller layout");
_Static_assert(sizeof(struct system_info_usb_device) == 48, "USB device layout");
_Static_assert(sizeof(struct system_info_usb_interface) == 16, "USB interface layout");

_Static_assert(sizeof(struct system_info_identity) == 128, "system identity layout");
_Static_assert(sizeof(struct system_info_cpu) == 64, "system CPU layout");
_Static_assert(sizeof(struct system_info_memory) == 24, "system memory layout");
_Static_assert(sizeof(struct system_info_pci) == 16, "system PCI layout");
_Static_assert(sizeof(struct system_info_pci_function_request) == 24, "PCI function request layout");
_Static_assert(sizeof(struct system_info_pci_function) == 16, "PCI function layout");

#endif
