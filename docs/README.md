# Documentation

Start with the repository [README](../README.md) to build and boot Pyxis.
For everyday guest use, read the [shell guide](userland/shell.md) or follow the
[edit/build/run walkthrough](development/edit-build-run.md).

## Find a reference

| Area | What belongs here | Useful starting point |
| --- | --- | --- |
| [Kernel](kernel/) | Execution, memory ownership, scheduling and clocks | [SMP ownership](kernel/smp.md), [ACPI interpreter](kernel/acpi.md) |
| [Interfaces](interfaces/) | Processes, capabilities, IPC and resource protocols | [System pointer](interfaces/pointer.md), [Kernel log](interfaces/kernel-log.md), [System information](interfaces/system-information.md), [PCM audio sessions](interfaces/audio.md), [processes and capabilities](interfaces/processes.md), [execution groups](interfaces/execution-groups.md) and their [termination ownership](interfaces/execution-group-termination.md) |
| [Devices](devices/) | Hardware discovery, drivers, networking and storage backends | [PS/2 mouse](devices/mouse.md), [hardware inspection](devices/hardware-inspection.md), [HDA playback engine](devices/hda.md), [xHCI](devices/usb-xhci.md), [USB enumeration](devices/usb-enumeration.md), [interrupt IN](devices/usb-interrupt-in.md), [USB storage](devices/usb-storage.md), [USB installation](devices/usb-installation.md), [Networking](devices/networking.md), [DHCP](devices/dhcp.md), [net0 selection](devices/net0-selection.md), [host filesystem](devices/virtio-fs.md), [native filesystem mounts](devices/native-readonly-filesystem.md), [kernel writer](devices/filesystem-native-adapter.md) and [installer authority](devices/installer-authority.md) |
| [Userland](userland/) | Shell, libraries, configuration, services and port usage | [System layout](userland/system-layout.md), [Everyday commands](userland/everyday-commands.md), [native installer](userland/installer.md), [system updates](userland/system-updates.md), [Mousetest](userland/mousetest.md), [Terminal behavior](userland/terminal.md), [space layers](userland/space-layers.md), [terminal sessions](userland/terminal-sessions.md), [multiplexer](userland/multiplexer.md), [remote terminals](userland/remote-terminal.md), [foreground interruption](userland/foreground-interruption.md), [libc I/O](userland/stdio.md), [Fastfetch](userland/fastfetch.md), [uniq](userland/uniq.md), [wc, tail and sort](userland/wc-tail-sort.md), [vi](userland/vi.md), [Links](userland/links.md), [DevilutionX](userland/devilutionx.md) (opt-in), [lspci](userland/lspci.md), [lsusb](userland/lsusb.md) |
| [Development](development/) | Build/SDK integration, debugging and performance reports | [SDK and repositories](development/sdk-and-repositories.md), [configuration](development/configuration.md), [LLVM toolchain](development/llvm-toolchain.md), [C++ in userspace](development/cxx-userspace.md), [SDL2](development/sdl2.md), [network throughput](development/network-throughput.md), [HD Audio investigation](development/audio-investigation.md), [remote debugging](development/remote-debugging.md), [GDB](development/gdb.md), [Linux npfs mounts](development/npfs-linux-mount.md) |

Keep implementation contracts beside their subsystem. Supporting measurement
data and probe artifacts live with the corresponding development or userland
reference. The filesystem encoding and host-tool contracts, and port-specific
notes, remain authoritative in their separately versioned repositories.

The [system pointer](interfaces/pointer.md) covers surface ownership, geometry,
lock, cursor images and selection boundaries. [Mousetest](userland/mousetest.md)
exercises its native sessions; [pointer qualification](development/system-pointer-qualification.md)
records runtime evidence and limits.

The [display reference](kernel/display.md) covers driver selection, transactional
local resizing, panic ownership and application adaptation.
[Screen capture](interfaces/screen-capture.md) describes whole-screen CAPTURE
authority, the immutable FILE result and presenter handoff.
The [screenshot command](userland/screenshot.md) covers staged PNG output and
the explicit host-download workflow. The
[qualification report](development/screenshot-qualification.md) records QEMU
coverage and measured cost, and the
[native ThinkPad check](development/screenshot-qualification.md#native-thinkpad-check).

The [RTL8111 driver](devices/rtl8111.md) supports the ThinkPad's built-in port.
Its [hardware profile](devices/rtl8111-hardware.md) records ownership and register
contracts; [qualification](development/rtl8111-qualification.md) covers VFIO and
the owner-run native cold/PXE boot with the dock attached.

## Work in progress

The [Bluetooth investigation report](development/bluetooth-investigation.md)
summarizes the AX200 passthrough evidence through identification of the MX Master
3S. The completed investigation is the [AX200 reference](devices/ax200-bluetooth.md).
Its [mouse milestone](wip/bluetooth-mouse.md) records the accepted direction and
completed task 1 contracts. The [runtime HCI reference](devices/bluetooth-hci.md) describes the completed
tasks 2–3 transport/firmware readiness, native cold/warm evidence and remaining
radio/input qualification limits. Later tasks require
explicit assignment.

Use the [milestone index](wip/boot-sdk-ports.md) to find active work and parked
proposals. Files in [wip](wip/) describe unfinished work or design directions;
they do not establish implemented behavior. Existing costs and revisit conditions
belong in [technical debt](technical-debt.md).

Follow [AGENTS.md](../AGENTS.md) for the collaboration workflow. When a milestone
finishes, turn its WIP document into a reference in the appropriate subject folder
and update its links; Git retains the completed planning history.

[Small-port workflow feedback](development/porting-feedback.md) records the uniq
handoff lessons and the bounded experiment with lighter port planning.
