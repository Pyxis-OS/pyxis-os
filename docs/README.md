# Documentation

Start with the repository [README](../README.md) to build and boot Pyxis.
For everyday guest use, read the [shell guide](userland/shell.md) or follow the
[edit/build/run walkthrough](development/edit-build-run.md).

## Find a reference

| Area | What belongs here | Useful starting point |
| --- | --- | --- |
| [Kernel](kernel/) | Execution, memory ownership, scheduling and clocks | [SMP ownership](kernel/smp.md) |
| [Interfaces](interfaces/) | Processes, capabilities, IPC and resource protocols | [System information](interfaces/system-information.md), [processes and capabilities](interfaces/processes.md), [execution groups](interfaces/execution-groups.md) and their [termination ownership](interfaces/execution-group-termination.md) |
| [Devices](devices/) | Hardware discovery, drivers, networking and storage backends | [hardware inspection](devices/hardware-inspection.md), [xHCI](devices/usb-xhci.md), [USB enumeration](devices/usb-enumeration.md), [USB storage](devices/usb-storage.md), [Networking](devices/networking.md), [DHCP](devices/dhcp.md), [host filesystem](devices/virtio-fs.md), [native filesystem mounts](devices/native-readonly-filesystem.md), [kernel writer](devices/filesystem-native-adapter.md) and [installer authority](devices/installer-authority.md) |
| [Userland](userland/) | Shell, libraries, configuration, services and port usage | [Native installer](userland/installer.md), [Terminal behavior](userland/terminal.md), [terminal sessions](userland/terminal-sessions.md), [remote terminals](userland/remote-terminal.md), [foreground interruption](userland/foreground-interruption.md), [libc I/O](userland/stdio.md), [Fastfetch](userland/fastfetch.md), [uniq](userland/uniq.md), [vi](userland/vi.md), [lspci](userland/lspci.md), [lsusb](userland/lsusb.md) |
| [Development](development/) | Build/SDK integration, debugging and performance reports | [SDK and repositories](development/sdk-and-repositories.md), [configuration](development/configuration.md), [GDB](development/gdb.md) |

Keep implementation contracts beside their subsystem. Supporting measurement
data and probe artifacts live with the corresponding development or userland
reference. The filesystem encoding and host-tool contracts, and port-specific
notes, remain authoritative in their separately versioned repositories.

The [RTL8111 driver](devices/rtl8111.md) supports the ThinkPad's built-in port.
Its [hardware profile](devices/rtl8111-hardware.md) records ownership and register
contracts; [qualification](development/rtl8111-qualification.md) covers VFIO and
the owner-run native cold/PXE boot with the dock attached.

## Work in progress

Use the [milestone index](wip/boot-sdk-ports.md) to find active work and parked
proposals. Files in [wip](wip/) describe unfinished work or design directions;
they do not establish implemented behavior. Existing costs and revisit conditions
belong in [technical debt](technical-debt.md).

Follow [AGENTS.md](../AGENTS.md) for the collaboration workflow. When a milestone
finishes, turn its WIP document into a reference in the appropriate subject folder
and update its links; Git retains the completed planning history.

[Small-port workflow feedback](development/porting-feedback.md) records the uniq
handoff lessons and the bounded experiment with lighter port planning.
