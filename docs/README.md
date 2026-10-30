# Documentation

Start with the repository [README](../README.md) to build and boot Pyxis.
To learn how to use Pyxis, read the [user manual](manual/README.md). For
everyday guest use, see also the [shell guide](userland/shell.md) and the
[edit/build/run walkthrough](development/edit-build-run.md).
The [release history](releases.md) lists every tagged version; how a release is
prepared is in the [release process](development/release-process.md).
The [website source](../site/README.md) builds pyxis.nexus from the versioned
manual and gallery without keeping another copy of their Markdown.

## Find a reference

| Area | What belongs here | Useful starting point |
| --- | --- | --- |
| [Kernel](kernel/) | Execution, memory ownership, scheduling and clocks | [SMP ownership](kernel/smp.md), [VM ownership and retirement](kernel/vm-ownership.md), [CPU performance policy](kernel/cpu-power.md), [program loading](kernel/program-loading.md), [ACPI interpreter](kernel/acpi.md), [Renoir flips](kernel/renoir-flip.md) |
| [Interfaces](interfaces/) | Processes, capabilities, IPC and resource protocols | [System pointer](interfaces/pointer.md), [clipboard](interfaces/clipboard.md), [Kernel log](interfaces/kernel-log.md), [System information](interfaces/system-information.md), [process listing](interfaces/process-listing.md), [PCM audio sessions](interfaces/audio.md), [processes and capabilities](interfaces/processes.md), [capability forwarders](interfaces/forwarders.md), [process environment](interfaces/environment.md), [command-directory lists](interfaces/command-search.md), [native operation completions](interfaces/native-completion.md), [clock precision](interfaces/clocks.md), [HTTP resources and transactions](interfaces/http-transactions.md), [native threads](interfaces/threads.md), [readiness sets](interfaces/readiness-sets.md), [execution groups](interfaces/execution-groups.md) and their [termination ownership](interfaces/execution-group-termination.md), [package formats](interfaces/packages.md), [kernel ABI and release ABIs](interfaces/abi.md) |
| [Devices](devices/) | Hardware discovery, drivers, networking and storage backends | [PS/2 mouse](devices/mouse.md), [hardware inspection](devices/hardware-inspection.md), [HDA playback engine](devices/hda.md), [xHCI](devices/usb-xhci.md), [USB enumeration](devices/usb-enumeration.md), [interrupt IN](devices/usb-interrupt-in.md), [USB boot input](devices/usb-hid.md), [USB storage](devices/usb-storage.md), [NVMe](devices/nvme.md), [USB installation](devices/usb-installation.md), [Networking](devices/networking.md), [DHCP](devices/dhcp.md), [interface selection](devices/net0-selection.md), [host filesystem](devices/virtio-fs.md), [native filesystem mounts](devices/native-readonly-filesystem.md), [kernel writer](devices/filesystem-native-adapter.md), [explicit durability](devices/explicit-durability.md) and [installer authority](devices/installer-authority.md), [FAT32 volumes](devices/fat32-volumes.md) and [inspection](devices/fat32-inspection.md) |
| [Userland](userland/) | Shell, libraries, configuration, services and port usage | [System layout](userland/system-layout.md), [Everyday commands](userland/everyday-commands.md), [Neovim](userland/neovim.md), [clangd](userland/clangd.md), [LuaLS](userland/lua-language-server.md), [checking the installed configuration](userland/configuration-editor.md), [space editor](userland/spaces.md), [native installer](userland/installer.md), [package and release upgrades](userland/package-upgrades.md), [Fedora GRUB → Limine](userland/grub-limine-chainload.md), [system updates](userland/system-updates.md), [program bundles](userland/program-bundles.md), [script authority](userland/script-authority.md), [command search](userland/command-search.md), [machine settings](userland/machine-settings.md), [volume controls](userland/audio-volume.md), [Mousetest](userland/mousetest.md), [Terminal behavior](userland/terminal.md), [space layers](userland/space-layers.md), [terminal sessions](userland/terminal-sessions.md), [multiplexer](userland/multiplexer.md), [remote terminals](userland/remote-terminal.md), [foreground interruption](userland/foreground-interruption.md), [libc I/O](userland/stdio.md), [font rendering](userland/fonts.md), [blurgtext](userland/blurgtext.md), [libc threads](userland/threads.md), [shared SHA-256](userland/sha256.md), [working path and environment](userland/process-state.md), [Fastfetch](userland/fastfetch.md), [uniq](userland/uniq.md), [wc, tail and sort](userland/wc-tail-sort.md), [grep](userland/grep.md), [Caddy](userland/caddy.md), [Git](userland/git.md), [cpuclock](userland/cpuclock.md), [procs](userland/procs.md), [jq](userland/jq.md), [vi](userland/vi.md), [Links](userland/links.md), [Minesweeper](userland/minesweeper.md), [Chocolate Doom](userland/chocolate-doom.md), [Chocolate Quake](userland/chocolate-quake.md), [OpenArena](userland/openarena.md), [DevilutionX](userland/devilutionx.md) (personal package), [EDuke32](userland/eduke32.md) (personal package), [lspci](userland/lspci.md), [lsusb](userland/lsusb.md), [net](userland/net.md), [acpidump](userland/acpidump.md) |
| [Development](development/) | Build/SDK integration, debugging and performance reports | [SDK and repositories](development/sdk-and-repositories.md), [configuration](development/configuration.md), [LLVM toolchain](development/llvm-toolchain.md), [Go cross-toolchain](development/go-toolchain.md), [Rust core+alloc](development/rust-toolchain.md), [LLVM Support](development/llvm-support.md), [C++ in userspace](development/cxx-userspace.md), [C++ filesystem and streams](development/cxx-filesystem-streams.md), [SDL2](development/sdl2.md), [SDL3](development/sdl3.md), [raylib](development/raylib.md), [library development packages](development/library-dev-packages.md), [SDL game ports](development/sdl-game-ports.md), [native Clang and LLD](development/hosted-tools.md), [network throughput](development/network-throughput.md), [HD Audio investigation](development/audio-investigation.md), [remote debugging](development/remote-debugging.md), [GDB](development/gdb.md), [Linux npfs mounts](development/npfs-linux-mount.md) |

Keep implementation contracts beside their subsystem. Supporting measurement
data and probe artifacts live with the corresponding development or userland
reference. The filesystem encoding and host-tool contracts, and port-specific
notes, remain authoritative in their separately versioned repositories.

[File metadata](interfaces/file-metadata.md) defines live identity, sampled
modification time and independent validity across native and provider backing.
[Native atomic append](interfaces/atomic-append.md) defines bounded EOF placement,
libc/shell/language use and qualification limits.
[Native hard links](interfaces/native-hard-links.md) defines npfs alias authority,
counted lifetime, feature activation and explicit durability.

The [system pointer](interfaces/pointer.md) and [boot input policy](interfaces/input-policy.md)
cover surface ownership, geometry,
lock, cursor images and selection boundaries. [volume controls](userland/audio-volume.md), [Mousetest](userland/mousetest.md)
exercises its native sessions; [pointer qualification](development/system-pointer-qualification.md)
records runtime evidence and limits.

The [display reference](kernel/display.md) covers driver selection, transactional
local resizing, panic ownership and application adaptation. The
[Intel two-plane scaling milestone](wip/intel-two-plane-scaling.md) records the
accepted hardware-scaled game and native-resolution UI direction; implementation
starts with read-only resource qualification.
The [pointer input reference](devices/pointer-input-quality.md) covers report
USB mice, shared scroll policy and calibrated Synaptics activation/filtering.
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

The [hardware-assumption audit](wip/hardware-assumptions.md) ranks remaining
boot and device-coverage risks for owner selection; it does not authorize fixes.

The [Bluetooth investigation report](development/bluetooth-investigation.md)
summarizes the AX200 passthrough evidence through identification of the MX Master
3S. The completed investigation is the [AX200 reference](devices/ax200-bluetooth.md).
Its [mouse milestone](wip/bluetooth-mouse.md) records the accepted direction and
completed task 1 contracts. The [runtime HCI reference](devices/bluetooth-hci.md) describes the completed
tasks 2–3 transport/firmware readiness, native cold/warm evidence and remaining
radio/input qualification limits. Later tasks require
explicit assignment.

The [native Wi-Fi interface](interfaces/wifi.md) describes READ/SCAN/CONNECT,
held open links and lifetime; the [AX200 controller](devices/ax200-wifi.md)
covers firmware, ownership, resources and limits. Stages 1–4 deliver legacy
2.4 GHz open and WPA2-CCMP links, independent DHCP and verified data, with wired
management retained through the [multiple-interface core](devices/network-interfaces.md).
The [Wi-Fi plan](wip/wifi.md) continues with vela RTL8922AE, then WPA3; AX200
5 GHz and the Deck remain later.

The [package reference](interfaces/packages.md) describes implemented data,
program and development packages and their command/SDK views. The
[updater milestone](wip/pkg-upgrade.md) records QEMU-qualified `pkg upgrade` and
its remaining native upgrade gate. Dynamic linking remains future work.

The [gallery](gallery/README.md) is the project's screenshot album.

Use the [milestone index](wip/boot-sdk-ports.md) to find active work and parked
proposals, and [completed milestones](wip/completed.md) for finished ones. Files in [wip](wip/) describe unfinished work or design directions;
they do not establish implemented behavior. Existing costs and revisit conditions
belong in [technical debt](https://git.internal/PyxisOS/pyxis-os/issues?type=all&state=all&labels=4).

Follow [AGENTS.md](../AGENTS.md) for the collaboration workflow. When a milestone
finishes, turn its WIP document into a reference in the appropriate subject folder
and update its links; Git retains the completed planning history.

[Small-port workflow feedback](development/porting-feedback.md) records the uniq
handoff lessons and the bounded experiment with lighter port planning.
