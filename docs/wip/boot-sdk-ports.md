# Development milestone index

Status: the current state of planned work, updated 2026-10-08. Nothing here
authorizes implementation: each milestone settles its decisions in its own
document before code work starts. Milestone order is flexible; the owner's
current choice wins. Everyday use for simple tasks guides the order; website
hosting is one future application, not the completion target. Fixes for major
problems found on the ThinkPad remain allowed in any track.

Implemented behavior lives in the subsystem references listed in the
[documentation index](../README.md). Completed milestones keep no WIP document.

## Recently completed

- [HD Audio investigation](../development/audio-investigation.md), 2026-10-08:
  unmerged QEMU probes established controller/codec commands and known PCM
  playback; the Fedora dump identifies ALC257, but native Pyxis audio
  qualification remains open and the production milestone remains proposed.
- [Kernel random generator](../devices/random-generator.md), 2026-10-08:
  BSP-owned ChaCha20 with the accepted OpenBSD rekey construction, hardware
  seed/reseed policy and unchanged random grant; matched VirtIO/CPU qualification
  recorded, with [trust and availability limits](../technical-debt.md#random-generator-trust-and-availability).
- [Deadline sleep wakeups](../kernel/timekeeping.md), 2026-10-08: per-CPU
  one-shot LAPIC deadlines with 120 Hz preemption kept; Quake's 72 Hz cap went
  from 49 to 70 FPS in QEMU and was
  [checked natively](../technical-debt.md#sleep-wake-granularity).
- [AX200 Bluetooth investigation](../devices/ax200-bluetooth.md),
  2026-10-08: warm-host QEMU passthrough through identification of the MX Master
  3S; [final report](../development/bluetooth-investigation.md). Native Pyxis
  evidence is inventory only; production mouse work remains proposed below.
- [SDL2 with a native backend](../development/sdl2.md), 2026-10-08: upstream
  SDL 2.32.10 for graphical ports, the SDK's CMake toolchain file, and
  [DevilutionX](../userland/devilutionx.md) as an opt-in, personal-use first
  consumer; [checked natively](../technical-debt.md#sdl2-and-devilutionx-native-qualification).
- [Screenshots](../userland/screenshot.md), 2026-10-08: reusable zlib/libpng
  ports, native CAPTURE and staged PNG output with explicit remote download;
  [checked natively](../development/screenshot-qualification.md#native-thinkpad-check).
- [Terminal multiplexer](../userland/multiplexer.md), 2026-10-08: the accepted
  first slice, with up to eight equal/BSP panes and colored scrollback.
- [C++ in userspace](../development/cxx-userspace.md), 2026-10-08: libc++,
  libc++abi and libunwind in the SDK, with fmt as the first C++ port.
- [Graphics and terminal layers](../userland/space-layers.md), 2026-10-08:
  Super+Up/Down; [checked natively](../technical-debt.md#space-layer-qualification).
- [Runtime SMP](../kernel/smp.md), 2026-10-06, with
  [topology-aware placement](../kernel/smp.md#placement-and-migration).
- [Mounting Pyxis volumes on Linux](../development/npfs-linux-mount.md), 2026-10-06.
- [System layout](../userland/system-layout.md), 2026-10-07: `boot://`,
  `bin://`, boot init with a Lua configuration, and a persistent home.
- [ACPI](../kernel/acpi.md), 2026-10-07: power-off, restart, the power button
  and battery.
- [Display drivers and resizing](../kernel/display.md), 2026-10-07.
- [Remote debugging without serial](../development/remote-debugging.md), 2026-10-07.
- [Everyday commands](../userland/everyday-commands.md), 2026-10-07.
- [LLVM toolchain on the host](../development/llvm-toolchain.md), 2026-10-07.
- [npfs](../devices/filesystem-readonly.md) and its
  [kernel writer](../devices/filesystem-native-adapter.md), with the
  [native installer](../userland/installer.md), [system updates](../userland/system-updates.md)
  and [USB installation](../devices/usb-installation.md); qualified natively on
  the ThinkPad.
- ThinkPad bring-up: see the [target notes](../targets/t14-gen1-amd/notes.md#native-status).

## Assigned

Chosen by the owner, each starting with a proposal:

- **Codex** (2026-10-08): [HD Audio playback proposal](hda-playback.md),
  following the completed [QEMU investigation](../development/audio-investigation.md).
  Three defaults accepted 2026-10-08: BSP kernel worker/mixer, at most eight
  exclusive per-space sessions, 48 kHz S16LE stereo, and 4 × 10 ms DMA/80 ms
  queues as starting tuning. First controller/codec bring-up task assigned
  2026-10-08; [private engine and matched qualification delivered for review](../development/experiments/audio-task1/README.md).
  Public sessions/mixing and IRQ/refill remain later tasks.
  QEMU closure with
  a later ThinkPad native batch is carried forward for confirmation at closure;
  native playback remains unqualified. Quake can produce sound
  from its main loop; SDL2/DevilutionX audio waits on real userspace
  [threads](scheduling-and-threads.md).
- **Codex** (2026-10-08): [MX Master 3S milestone proposal](bluetooth-mouse.md),
  task 1 complete for documentation/contracts after native batch #547. All owner
  [decision rounds and alpha coordination](bluetooth-task1-contracts.md) are
  accepted 2026-10-08; pointer tasks 1+2 are merged in #545. Later measurements
  remain prerequisites. Task 2 runtime HCI transport is not assigned or started.
- **Codex alpha** (owner update, 2026-10-08): [A system pointer](pointer.md).
  Proposal #530 and tasks 1+2 are merged: Pyxis #545, userland #164, ports #65/#66.
  The owner authorizes task 3: local TTY selection, trusted mux terminal control,
  terminal spatial queue/readiness and mux selection/wheel history. The owner's
  addition makes graphics pointer subscriptions waitable through `wait_many`;
  the later SDL blocking-event fix remains beta's work. Fresh main `abbeded`
  has a pre-code [baseline](../development/system-pointer-qualification.md#task-3-baseline)
  recorded in [#550](https://git.internal/PyxisOS/pyxis-os/pulls/550).
  [Three task 3 defaults](pointer.md#task-3-planning) are accepted on 2026-10-08;
  task 3 is delivered in #550 with published userland #166 (`63d4324`),
  which merges before the parent. [Qualification](../development/system-pointer-qualification.md#task-3-qualification)
  records runtime/source limits and matched samples. Tasks 4 and 5 need separate authorization.
  Matched QEMU checks suffice for closure; [native PS/2 validation is deferred](../technical-debt.md#native-system-pointer-qualification)
  to the owner's ThinkPad batch after Bluetooth investigation. Devices stay PS/2;
  clipboard and USB HID implementation remain separate. Bluetooth's accepted
  conditional source-loss rule is recorded for future integration, without a
  second input source.
- **Claude** (2026-10-08): [network throughput](network-throughput.md), TCP
  toward the owner's gigabit LAN. Three decisions are accepted; segment size
  and windows are implemented, with Nagle and the `ttcp -r` direction open.
  The native baseline waits for the owner's next ThinkPad batch. The
  [remote transfer](remote-file-transfer.md#transfer-throughput) reply-read
  fix merged in #554; its native re-timing is pending.
- **The owner:** the Java virtual machine of
  [developing inside Pyxis](in-pyxis-development.md).

## Open

- [File transfer through the remote terminal](remote-file-transfer.md):
  drag-and-drop upload awaits the owner's GUI-drop check from Linux and macOS.
- [Developing inside Pyxis](in-pyxis-development.md): the extra-spaces check on
  the installed ThinkPad is open.
- [Everyday gaps](everyday-gaps.md): small things noticed in use.

## Candidates for the next milestone

Other candidates; current assignments are listed above.

- [Terminal applications](terminal-applications.md): a single-panel file
  navigator, then operations between navigators.
- [PDCurses](application-ports.md#libraries-and-terminal-tools) over libterm,
  with one terminal application, and [SQLite](application-ports.md#libraries-and-terminal-tools)
  through a native VFS.
- [Developer tools](later-os-directions.md#developer-tools): a capability
  inspector and `top`.
- [Clipboard](clipboard.md): typed objects with a text form, local and shared
  across spaces.
- USB HID mice after [USB interrupt IN](../devices/usb-interrupt-in.md): HID
  boot protocol and input integration, separate from the PS/2 system pointer.
- [Audio](later-os-directions.md#audio), starting with Intel HD Audio playback.
- [Remote desktop](remote-desktop.md): a view-only RFB server over screen capture
  first, then remote input after the system pointer.
- Clang running on Pyxis, the [third LLVM milestone](toolchains-and-runtimes.md#llvmclang-transition-and-hosting),
  now that [C++ in userspace](../development/cxx-userspace.md) is complete.
- System layout follow-ups: network configuration on the pool instead of the
  archive ([technical debt](../technical-debt.md#archive-only-network-configuration))
  and the [boot configuration checker](boot-configuration-checker.md).
- [Threads and SMP follow-ups](scheduling-and-threads.md), including serial
  services off the BSP.
- Physical GPU drivers, after the [display milestone](../kernel/display.md);
  the owner prepares the hardware.
- [Power and ACPI follow-ups](later-os-directions.md#power-and-acpi).

## Directions

Parked directions and investigations; promote one to a milestone when its
prerequisites and result are clear.

- [Later OS directions](later-os-directions.md): execution lifecycle, backend
  interfaces, networking, device ownership, developer tools, audio, Bluetooth,
  clock source, power, storage.
- [Application port candidates](application-ports.md), including SDL2/GrafX2,
  Peanut-GB, Chocolate Duke3D, DevilutionX and a wasm3 investigation.
- [Hosted toolchains and language runtimes](toolchains-and-runtimes.md), the
  [Go runtime investigation](go-runtime.md) and the
  [Neovim/libuv investigation](neovim-libuv.md).
- [Building software on Pyxis](source-builds.md): after hosted Clang, ports and
  non-rescue userland build from source on the installed system, Gentoo style,
  until a package manager exists.
- [Desktop and graphics](desktop-graphics.md), including where to start on the
  owner's compositor.
- [Interchangeable providers](interchangeable-providers.md): programs depend on
  protocols, and launchers choose who serves them, so a compositor can serve the
  same display, pointer and keyboard interfaces as the kernel.
- [Users and authority](users-and-authority.md), a cross-cutting design
  checkpoint, and [credentials and biometric unlock](credentials-and-biometrics.md),
  parked after local users.
- [Spaces](spaces.md), including [Asterism](spaces.md#asterism), and
  [filesystems and namespaces](vfs.md), working drafts.
- [Control, events and faults](control-events-faults.md), Pyxis's answer to
  signals, tied to Continuum.
- [Userspace scheme providers](userspace-scheme-providers.md).
- [Bluetooth mouse](bluetooth-mouse.md): accepted kernel HCI/userspace stack
  direction, explicit Secure Connections enrollment and cold/native closure;
  remaining policy decisions are open.
- [Selecting existing build artifacts](build-artifact-reuse.md) and the
  postponed [host development overlay](host-development-overlay.md).

## Agreed boundaries

- Init performs setup and hands off to the shell. General service supervision
  and restart policy remain deferred; remote terminal execution groups have the
  explicit lifetime contract in their milestone. Start with a shebang shell
  script, fail on script errors and use an explicit session launch; `exec` comes
  later.
- Userspace owns libc, libpyxis, libterm, startup and applications. Pyxis owns
  the public ABI headers and toolchain integration, and assembles the SDK,
  kernel and boot image.
- Export headers, build runtime libraries, assemble the SDK, then build apps and
  ports. Initially pin the new userspace/ports repositories as submodules.
- The owner handles repository creation, dispatch integration and compiler
  container publication. Ordinary builds consume the prebuilt compiler and
  evolving SDK; they do not rebuild the toolchain.
- Init mounts optional `host://` before launching the shell and passes the
  selected directory grant to the session.

## User and permission design checkpoint

Multiple users with restricted permissions are a requirement. The
[users and authority notes](users-and-authority.md) record stable principals,
policy-based acquisition, runtime capabilities and prospective permission changes.
They retain the unresolved enforcement and lifecycle decisions before persistent
ownership is implemented. External login and account UI are separate work.
The [credentials and biometric unlock direction](credentials-and-biometrics.md)
is parked after local users; it adds no tasks to current milestones.

## Completing a milestone

Rewrite the completed milestone document around the implemented behavior and
useful interface/usage guidance, then move it from `docs/wip` to `docs` and update
links. Remove the planning history and completed checklist; the original remains
in Git history. Carry forward relevant deferred work into another WIP or
technical-debt document. Do not retain a duplicate archive of the old plan.
