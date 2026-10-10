# Development milestone index

Status: the current state of planned work. Nothing here
authorizes implementation: each milestone settles its decisions in its own
document before code work starts. Milestone order is flexible; the owner's
current choice wins. Everyday use for simple tasks guides the order; website
hosting is one future application, not the completion target. Fixes for major
problems found on the ThinkPad remain allowed in any track.

Implemented behavior lives in the subsystem references listed in the
[documentation index](../README.md). Completed milestones keep no WIP document.

Each entry names the milestone, who it is assigned to and its current task, in
a sentence or two. Change an entry only when work is assigned, a milestone
completes or the owner changes the plan. Progress, measurements and review
status belong in the milestone document and the PR.

## Recently completed

- [USB HID boot keyboards and mice](../devices/usb-hid.md), 2026-10-10: one shared
  keyboard path, USB 2 hub chains and bounded leaf hotplug (#654); QEMU and
  ThinkPad keyboard and composite mouse input qualified, with
  [native coverage still open](../technical-debt.md#usb-hid-native-coverage).
- [Renoir flip presentation](../kernel/renoir-flip.md), 2026-10-10: opt-in
  two-surface GOP page flips (#631, #632, #638, #647); normal flips and
  Quake and Chocolate Quake play natively qualified, default off, with the
  [remaining qualification](../technical-debt.md#renoir-flip-backend-qualification)
  and [poll-cost qualification](../development/experiments/renoir-poll-cost/README.md)
  (about 67% less BSP observation elapsed per frame, owner-run A–B–A–B). Presentation
  [timing and pacing](presentation-timing.md) keeps its unassigned step 3.
- [Cheaper timekeeping](../kernel/timekeeping.md#clock-reads-in-timer-handling),
  2026-10-10: fewer clock reads per timer event (#571) and the TSC with HPET
  fallback (#577), natively qualified; [measurements](../development/experiments/tsc-clock/README.md).
- [SDL game ports](../development/sdl-game-ports.md), 2026-10-09: Chocolate
  Doom and Chocolate Quake in ordinary images and EDuke32 as an opt-in
  personal build, all without sound (#623, #634, #646); QEMU comparisons
  recorded, native timedemos and play checked, with
  [remaining native checks](../technical-debt.md#sdl-game-ports-native-qualification).
- [Volume controls](../userland/audio-volume.md), 2026-10-09: master/per-space
  gain and bar widgets; native listening, mute and reboot default checked.
  [Remaining native regression](../technical-debt.md#hd-audio-volume-native-regression).
- [HTTP redirects](../userland/http-fetch.md#redirect-chains), 2026-10-09:
  bounded delegated-provider chains and FILE response metadata, with Links
  adopting the final URL; [controlled QEMU qualification](../development/experiments/http-redirects/README.md).
- [HD Audio playback](../devices/hda.md), 2026-10-09: BSP-owned analog engine and
  [up to eight PCM sessions](../interfaces/audio.md), one per space; native speaker/headphone
  tones and eleven-minute eight-session silence passed, ninth refused.
  [Live jack switching](../technical-debt.md#hd-audio-jack-routing-at-playback-start)
  and the [nested-QEMU limit](../technical-debt.md#hd-audio-sustained-eight-session-playback) remain debt.
- [Network throughput](../development/network-throughput.md), 2026-10-09:
  1460-byte segments on-link, 64 KiB windows and Nagle off; natively send went
  from 35.8 to 70.5 MiB/s and receive reaches 85 MiB/s with `ttcp -r`, with
  [remaining limits](../technical-debt.md#tcp-throughput-limits) recorded.
- [A system pointer](../interfaces/pointer.md), 2026-10-09: ordinary surface
  input, program cursors, relative lock and user escape, terminal selection and
  mux wheel browsing, and VirtIO hardware cursors. Tasks 1–4 are merged in
  #545/#550/#560. Task 5 (reference closure and default-cursor redraw) closed in
  [#562](https://git.internal/PyxisOS/pyxis-os/pulls/562). The
  [qualification report](../development/system-pointer-qualification.md) records
  matched QEMU checks and partial native evidence;
  [remaining native checks](../technical-debt.md#native-system-pointer-qualification)
  stay open. Clipboard and additional input sources remain separate milestones.
- [HD Audio investigation](../development/audio-investigation.md), 2026-10-08:
  unmerged QEMU probes established controller/codec commands and known PCM
  playback and identified ALC257; the production playback milestone is now closed
  in the [engine reference](../devices/hda.md).
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
- [Program image and initial stack capacity](../kernel/program-loading.md),
  2026-10-09: 256 MiB image span and high fixed eager 1 MiB guarded stacks;
  [matched costs and session memory](../development/experiments/program-capacity/README.md).
  Thread task 1 merged in #612; later thread tasks remain unassigned.
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

- **Codex epsilon** (2026-10-10): [bundles in bin://](bundles-in-bin.md),
  accepted same-name lookup and relocation of the editor/interpreter bundles;
  task 1 starts after the owner merges #686.
- **Codex alpha** (2026-10-10): [presenter whole-frame skipping](../kernel/presenter-frame-skipping.md),
  implemented with matched QEMU/native counters and the owner's functional pass;
  ready for review before merge. [Renoir cursor](renoir-hardware-cursor.md)
  task 1 is merged; task 2 and scaled game planes remain separate assignments.
- **Codex** (2026-10-10): [Neovim task 6 slice 4](neovim-groundwork.md#slice-4-contract-accepted-2026-10-10),
  native editor recipe, TUI and runtime, with QEMU tab/pane qualification; review
  before merge. Native qualification and the extended terminal profile follow.
- **Codex delta** (2026-10-10): [threads task 3](threads.md#shared-capability-and-vm-ownership),
  owner-assigned shared-table delivery, growth, reservations and owned inputs;
  still one user task. Tasks 1–2 are merged; later gates need separate assignment.
- **Codex epsilon** (2026-10-09): [Claude on Pyxis proposal](claude-on-pyxis.md),
  prepared HTTPS files and shell API testing, then the Lua harness; Hax after.
  Docs only. ZIP defaults are accepted in #639; delivery tasks need separate go-aheads.
- **Claude** (2026-10-10): [name limits and links](../userland/libc-portability.md#name-limits-and-symbolic-links):
  `NAME_MAX`, O_NOFOLLOW, ELOOP and ENAMETOOLONG from new kernel statuses.
  The [port sweep](../development/experiments/libc-port-sweep/README.md) (#683),
  Neovim task 6 slice 3 (#678), task 5
  (#672) and the [system power overlay](system-power-overlay.md) (#659) are
  merged; the overlay's installed-system flush check is pending.
  Presentation timing steps 1 and 2 (#610, #618),
  saved shell history (#609) and xfer pipelining (#600) are merged; xfer's
  native runs follow its
  [measurements and native steps](../development/experiments/xfer-pipelining/README.md).
- **Codex** (2026-10-08): [MX Master 3S milestone proposal](bluetooth-mouse.md),
  task 1 complete for documentation/contracts after native batch #547. All owner
  [decision rounds and alpha coordination](bluetooth-task1-contracts.md) are
  accepted 2026-10-08; pointer tasks 1+2 are merged in #545. Later measurements
  remain prerequisites. Task 2 runtime HCI transport is complete in #552, with
  [baseline and warm qualification](../development/experiments/bluetooth-runtime-hci/README.md).
  Task 3 firmware readiness is complete 2026-10-09, with owner-reported native
  cold upload/DDC readiness and warm skip in
  [#564](https://git.internal/PyxisOS/pyxis-os/pulls/564).
  Actual service/connection traffic remains task 4's qualification gate; later
  tasks need explicit assignment.
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
- [Network kernel debugger](network-debugger.md): checkpoint and guarded
  inspection implemented; task 3 assigned for both NICs, bridge and native
  read-only LAN attach during PXE bring-up.
- [Audio consumers](later-os-directions.md#audio): SDL2 and Quake adapters after
  the completed analog playback milestone.
- [Remote desktop](remote-desktop.md): a view-only RFB server over screen capture
  first, then remote input after the system pointer.
- Clang running on Pyxis, the [third LLVM milestone](toolchains-and-runtimes.md#llvmclang-transition-and-hosting),
  now that [C++ in userspace](../development/cxx-userspace.md) is complete.
- System layout follow-ups: network configuration on the pool instead of the
  archive ([technical debt](../technical-debt.md#archive-only-network-configuration))
  and the [boot configuration checker](boot-configuration-checker.md).
- [SMP follow-ups](scheduling-and-threads.md), including serial services off the BSP.
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
  [Neovim milestone proposal](neovim-libuv.md), with its re-check of the libuv
  investigation.
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
  tasks 1–2 are complete, with warm-only transport qualification.
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
