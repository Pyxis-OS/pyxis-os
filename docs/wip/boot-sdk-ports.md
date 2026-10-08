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

Chosen by the owner on 2026-10-07, each starting with a proposal:

- **Claude:** [C++ in userspace](cxx-userspace.md), cross-compiled C++
  programs with a runtime from the `pyxis-llvm` fork; decisions accepted, task 2
  (libc and SDK layout) in review. Clang running on Pyxis is a later milestone.
- **Codex 1:** [graphics and terminal layers in a space](space-layers.md),
  switched with Super+Up/Down.
- **Codex 2:** [screenshots](screenshots.md), a `screenshot` command writing
  PNG through new zlib and libpng ports, with the file fetched through
  pyxis-remote.
- **The owner:** the Java virtual machine of
  [developing inside Pyxis](in-pyxis-development.md).

## Open

- [File transfer through the remote terminal](remote-file-transfer.md):
  drag-and-drop upload awaits the owner's GUI-drop check from Linux and macOS.
- [Developing inside Pyxis](in-pyxis-development.md): the extra-spaces check on
  the installed ThinkPad is open.
- [Everyday gaps](everyday-gaps.md): small things noticed in use.

## Candidates for the next milestone

None is selected.

- [Terminal applications](terminal-applications.md): a multiplexer (with
  scrollback; its three decisions are accepted), then a single-panel file
  navigator, then operations between navigators.
- [PDCurses](application-ports.md#libraries-and-terminal-tools) over libterm,
  with one terminal application, and [SQLite](application-ports.md#libraries-and-terminal-tools)
  through a native VFS.
- [Developer tools](later-os-directions.md#developer-tools): a capability
  inspector and `top`.
- [Audio](later-os-directions.md#audio), starting with Intel HD Audio playback.
- Clang running on Pyxis, the [third LLVM milestone](toolchains-and-runtimes.md#llvmclang-transition-and-hosting),
  after C++ in userspace.
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
  interfaces, networking, device ownership, developer tools, audio, clock
  source, power, storage.
- [Application port candidates](application-ports.md), including SDL2/GrafX2,
  Peanut-GB, Chocolate Duke3D, DevilutionX and a wasm3 investigation.
- [Hosted toolchains and language runtimes](toolchains-and-runtimes.md), the
  [Go runtime investigation](go-runtime.md) and the
  [Neovim/libuv investigation](neovim-libuv.md).
- [Desktop and graphics](desktop-graphics.md), including where to start on the
  owner's compositor.
- [Users and authority](users-and-authority.md), a cross-cutting design
  checkpoint, and [credentials and biometric unlock](credentials-and-biometrics.md),
  parked after local users.
- [Spaces](spaces.md), including [Asterism](spaces.md#asterism), and
  [filesystems and namespaces](vfs.md), working drafts.
- [Userspace scheme providers](userspace-scheme-providers.md).
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
