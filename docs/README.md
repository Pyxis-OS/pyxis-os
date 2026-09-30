# Documentation

Start with the repository [README](../README.md) to build and boot Pyxis.
For everyday guest use, read the [shell guide](userland/shell.md) or follow the
[edit/build/run walkthrough](development/edit-build-run.md).

## Find a reference

| Area | What belongs here | Useful starting point |
| --- | --- | --- |
| [Kernel](kernel/) | Execution, memory ownership, scheduling and clocks | [SMP ownership](kernel/smp.md) |
| [Interfaces](interfaces/) | Processes, capabilities, IPC and resource protocols | [Processes and capabilities](interfaces/processes.md) |
| [Devices](devices/) | Hardware discovery, drivers, networking and storage backends | [Networking](devices/networking.md), [host filesystem](devices/virtio-fs.md) |
| [Userland](userland/) | Shell, libraries, configuration, services and port usage | [Terminal behavior](userland/terminal.md), [terminal sessions](userland/terminal-sessions.md), [libc I/O](userland/stdio.md) |
| [Development](development/) | Build/SDK integration, debugging and performance reports | [SDK and repositories](development/sdk-and-repositories.md), [GDB](development/gdb.md) |

Keep implementation contracts beside their subsystem. Supporting measurement
data and probe artifacts live with the corresponding development or userland
reference. The filesystem format/core and port-specific notes remain authoritative
in their separately versioned repositories.

## Work in progress

Use the [milestone index](wip/boot-sdk-ports.md) to find active work and parked
proposals. Files in [wip](wip/) describe unfinished work or design directions;
they do not establish implemented behavior. Existing costs and revisit conditions
belong in [technical debt](technical-debt.md).

Follow [AGENTS.md](../AGENTS.md) for the collaboration workflow. When a milestone
finishes, turn its WIP document into a reference in the appropriate subject folder
and update its links; Git retains the completed planning history.
