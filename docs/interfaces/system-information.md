# Native system information

The explicitly delegated `system_info` resource supplies synchronous queries
through [the system-information ABI](../../include/abi/system_info.h).
One READ right authorizes system-wide identity, CPU, allocator, PCI and USB inventory
observations.
There is no ambient query or acquisition syscall. Kernel bootstrap creates the
stateless authority for trusted init; ordinary local and remote launch paths
forward it explicitly. Restricted launches can omit it, and shell-launched
service providers do not receive it.

## Queries and meaning

Requests contain only a `message_header`, except that indexed PCI/USB queries
add an index. Replies are bounded records with fixed-size NUL-terminated strings
and zeroed unused bytes.

| Query | Reply | Meaning |
| --- | --- | --- |
| `SYSTEM_INFO_IDENTITY` | `system_info_identity` | `Pyxis OS`, `Caelum`, `x86_64`, and the source commit embedded in the running kernel |
| `SYSTEM_INFO_CPU` | `system_info_cpu` | Guest-visible BSP brand and online logical CPU count after successful SMP boot |
| `SYSTEM_INFO_MEMORY` | `system_info_memory` | Coherent allocator total, allocated and free bytes |
| `SYSTEM_INFO_PCI` | `system_info_pci` | PCI inventory state and retained function count |
| `SYSTEM_INFO_PCI_FUNCTION` | `system_info_pci_function` | One retained PCI function's address and identity |
| `SYSTEM_INFO_USB` | `system_info_usb` | Boot snapshot state and controller/device/interface counts |
| `SYSTEM_INFO_USB_CONTROLLER` | `system_info_usb_controller` | PCI identity, inspection state and advertised root-port count |
| `SYSTEM_INFO_USB_DEVICE` | `system_info_usb_device` | Root-port identity, speed and checked device descriptor fields |
| `SYSTEM_INFO_USB_INTERFACE` | `system_info_usb_interface` | Checked configuration/alternate interface classes |

Empty build-revision or CPU-brand strings explicitly mean unavailable fields;
other fields remain usable. CPU count is neither physical cores nor the CPU
allowance of a pinned process. The BSP brand is sampled once, not an inventory
of heterogeneous CPUs or an identification of the physical host. CPU hotplug
is not supported.

Memory is **Memory (allocator)**: capacity excludes permanently reserved frames,
and `total_bytes = allocated_bytes + free_bytes`. This is neither installed RAM,
Linux-style available memory nor process RSS. All holders of READ can observe
these global counters. Each memory query uses the existing
[BSP executor](../kernel/bsp-service-requests.md) with interrupts disabled, keeping
the PMM's ownership contract. It samples all counters together without memory
mutation, a new allocator lock or an AP read of allocator state. The caller
copies the reply after the request returns ownership and releases its storage.

Identity, CPU and PCI inventory data are immutable before scheduler startup
publishes them to user tasks; their queries need no BSP request. The object retains no process or
user-buffer pointer. Ordinary object references govern its lifetime and BSP
retirement frees it after its final reference ends.

## PCI inventory

`SYSTEM_INFO_PCI` reports the state of the boot inventory and how many functions
it retained:

| State | Meaning |
| --- | --- |
| `SYSTEM_INFO_PCI_UNAVAILABLE` | No supported configuration access, such as a missing MCFG; count is zero |
| `SYSTEM_INFO_PCI_INCOMPLETE` | Discovery found malformed topology, an unsupported header or could not retain a function; listed functions remain valid |
| `SYSTEM_INFO_PCI_COMPLETE` | Every reachable function was retained |

`SYSTEM_INFO_PCI_FUNCTION` takes an index below the count; larger indices return
`CALL_NOT_FOUND`. Each record holds the segment and bus/device/function, vendor
and device ID, base class, subclass, programming interface, revision and header
type without the multifunction bit. A reserved field is zero. Discovery reads
these values once and the queries never touch configuration space. The order is
stable for the boot but otherwise unspecified, so consumers sort if they need to.
Each function query walks the retained list, which is cheap for real inventories.

These records carry no BARs, capabilities, subsystem IDs, driver ownership or
kernel pointers. READ grants no configuration access, reset or rescan. Consumers
that want a consistent listing need no snapshot: the inventory does not change
after boot and there is no hotplug.
See [PCI discovery](../devices/pci.md#inventory) and [lspci](../userland/lspci.md).

## USB inventory

The USB snapshot is built asynchronously by the BSP controller workers. Until
all retained xHCI controllers finish their bounded startup inspection, the root
query returns `SYSTEM_INFO_USB_INITIALIZING` with zero counts. When xHCI is
build-disabled or PCI access is unavailable, it returns `SYSTEM_INFO_USB_UNAVAILABLE`.
Otherwise publication uses release/acquire ordering; indexed queries copy only
immutable final records and need no BSP service request.

`SYSTEM_INFO_USB_COMPLETE` means every discovered USB host controller was inspected
within the supported profile, and all its boot-present direct root devices and
advertised configurations were checked. `SYSTEM_INFO_USB_INCOMPLETE` retains
usable observations when a controller is unsupported/failed, PCI discovery is
incomplete, descriptors exceed retained budgets or a hub has uninspected descendants.
An empty complete inventory is valid. No controller count or port numbering is a
machine requirement.

Indexed requests use `system_info_usb_request`; indices at or above their respective
counts, including before publication, return `CALL_NOT_FOUND`. Controller records
include the PCI identity and inspection state, including unsupported host interfaces.
Device records use a controller index and one-based physical root-port number;
they are not Linux bus/address identifiers. `IDENTIFIED` distinguishes a checked
VID/PID from a connected port whose descriptors could not be inspected. Interfaces
have global device indices and configuration/interface/alternate identities. They
are descriptive observations, not assertions that a configuration or driver is active.

The snapshot does not update after runtime removal or controller failure. Device
strings, serials, hub descendants, raw descriptors, endpoint addresses, physical
addresses and kernel pointers are absent. READ grants no USB transfer or reset
access. Names are resolved in [lsusb](../userland/lsusb.md) from packaged data.

## Errors and library interface

Wrong protocol or operation is `CALL_BAD_OPERATION`; missing READ is
`CALL_DENIED`; a request payload of the wrong size or an undersized reply
capacity is `CALL_BAD_REQUEST`; invalid user storage is `CALL_BAD_BUFFER`. Invalid or omitted
handles follow the ordinary `CALL_BAD_HANDLE` path. No authority failure returns
synthetic observations.

Libpyxis exports `system_info_get_identity`, `system_info_get_cpu`,
`system_info_get_memory`, `system_info_get_pci`, `system_info_get_pci_function`
and the four `system_info_get_usb*` queries
through `<system_info.h>`. The PCI wrappers reject unknown states, nonzero
counts for an unavailable inventory, out-of-range device or function numbers,
the multifunction bit, vendor `ffff` and nonzero reserved fields. Pass an explicitly supplied
handle, typically `startup_resource("system_info")`. The wrappers validate the
reply and leave the caller's output unchanged on every failure. The SDK exports
both the ABI and library headers with the ordinary runtime build. USB helpers
check states, flags, speeds, reserved fields and fixed reply sizes; `lsusb` checks
controller/device/interface associations before formatting.

Uptime remains on [clock READ](../../include/abi/clock.h), whose monotonic epoch
begins at HPET initialization and omits earlier boot time. It makes no wall-time
guarantee across VM pauses or suspend. Dimensions remain on the application's
[console capability](../../include/abi/console.h), including remote terminals'
fixed dimensions. Neither is part of an atomic system-information snapshot.

## Build identity

Normal source builds embed `git rev-parse --short=12 HEAD` from the kernel's
checkout. A longer abbreviation can be returned when needed for uniqueness.
A generated header is refreshed on every Make invocation; only its content
change rebuilds the identity object. This follows branch/commit changes in
linked worktrees as well as ordinary checkouts. A missing Git revision becomes
an empty string; a source archive inside another checkout does not inherit the
outer repository's identity. Prebuilt kernels retain their embedded revision.

This identifies the base source commit, not a clean-tree attestation. No `-dirty`
suffix or input-filtering policy is implemented. Documentation and dependency
commits can change HEAD without changing kernel behavior. SDK or userland
revisions are not substituted for the running kernel's revision.

The packaged [Fastfetch command](../userland/fastfetch.md) consumes this
interface. No separate permanent diagnostic command was added. This interface
needs an updated SDK and userland build, not a compiler-container rebuild.
