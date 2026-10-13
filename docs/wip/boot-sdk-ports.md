# Development milestone index

Status: the current state of planned work. Nothing here authorizes
implementation: each milestone settles its decisions in its own document before
code work starts. Milestone order is flexible; the owner's current choice wins.
Everyday use for simple tasks guides the order; website hosting is one future
application, not the completion target. Fixes for major problems found on the
ThinkPad remain allowed in any track.

**Planning freeze (2026-10-11, until the owner lifts it):** no new milestones,
proposals or investigations, and no new tasks in existing milestones. Ideas go in
[later OS directions](later-os-directions.md) as short entries. Bugs and hardware
problems found in use are still fixed.

Implemented behavior lives in the subsystem references listed in the
[documentation index](../README.md). Completed milestones keep no WIP document
and are listed in [completed milestones](completed.md).

**Rule: task PRs never edit this index.** Edit it only when a milestone is
assigned, completes or the owner changes the plan. Progress, checkboxes,
measurements and review status live in the milestone's own document and the PR.
Each entry is one short paragraph, separated by blank lines and sorted
alphabetically by name within its section, so edits to different entries merge
without conflict.

## Assigned

Chosen by the owner, each starting with a proposal.

[BSP pinning inventory](bsp-pinning-inventory.md): owner-requested freeze exception,
assigned to Codex alpha 2026-10-13; read-only ownership/latency inventory and
first-slice proposal, coordinated with Claude's bounded audio/teardown work.

Developing inside Pyxis, the Java virtual machine: the owner, see
[developing inside Pyxis](in-pyxis-development.md).

[FAT32 mutation preflight cost](fat32-preflight-cost.md): owner-assigned
investigation (2026-10-13) before installed configuration publication; whole-volume
read/workspace costs are proposed for reduction without extending the lease.

[Hosted Clang and LLD](hosted-clang.md): tasks 1–5 delivered; installed bundles
compile, separately link and run natively ([#772](https://git.internal/PyxisOS/pyxis-os/pulls/772)).
Clang driving LLD is assigned to Codex delta, after the accepted minimal nested
groups prerequisite ([#788](https://git.internal/PyxisOS/pyxis-os/pulls/788),
[#789](https://git.internal/PyxisOS/pyxis-os/pulls/789)).

[Installer TUI and configuration delta editor](installer-tui.md): tasks 1–4
are delivered ([#786](https://git.internal/PyxisOS/pyxis-os/pulls/786)). Task 5's
publication/installed-activation proposal is assigned to Codex alpha; owner
decisions are pending. Activation waits for FAT32 preflight cost, and internal
coexistence also needs NVMe task 3. Later tasks need separate go-aheads.

[Native HTTP requests](native-http-requests.md): tasks 1–4 delivered, including
prepared transactions, streaming and libc/Lua adapters. Task 5's accepted shell
coordinator is in progress ([#780](https://git.internal/PyxisOS/pyxis-os/pulls/780),
draft [#785](https://git.internal/PyxisOS/pyxis-os/pulls/785)).

[Nested execution groups](nested-execution-groups.md): the minimal Clang-driver
prerequisite is accepted and assigned to Codex delta as a planning-freeze exception,
2026-10-13 ([#788](https://git.internal/PyxisOS/pyxis-os/pulls/788)). Implementation
is draft [#789](https://git.internal/PyxisOS/pyxis-os/pulls/789); broader nesting remains parked.

[Renoir hardware cursor](renoir-hardware-cursor.md): Codex alpha (2026-10-10), the
Fedora evidence was captured and analysed on 2026-10-11 (#685, draft). Task 2 is
blocked on hardware semantics, not on evidence;
[whole-frame skipping](../kernel/presenter-frame-skipping.md) is merged.

[USB storage read throughput](usb-read-throughput.md): owner-assigned scoped
freeze exception (2026-10-13); native baseline complete, bounded npfs read
aggregation proposed. Implementation awaits a separate go.

## Open

[Developing inside Pyxis](in-pyxis-development.md): the extra-spaces check on the
installed ThinkPad is open.

[Everyday gaps](everyday-gaps.md): small things noticed in use.

[File transfer through the remote terminal](remote-file-transfer.md): drag-and-drop
upload awaits the owner's GUI-drop check from Linux and macOS.

[Threads and SDL consumers](threads.md#delivery-gates): gates 1–8 delivered;
SDL workers/timers and audio callbacks merged (#778, #784), with the
[Chocolate Quake sound bundle](../userland/chocolate-quake.md)
([#787](https://git.internal/PyxisOS/pyxis-os/pulls/787)). Libuv pool, Hax and Go
adapters await separate assignments.

## Candidates for the next milestone

Other candidates; current assignments are listed above.

[Audio follow-ups](later-os-directions.md#audio): analog playback and SDL2/Quake
adapters are delivered; [SDL latency tuning](../technical-debt.md#sdl-audio-latency) remains deferred.

[Claude on Pyxis](claude-on-pyxis.md): design accepted 2026-10-09 (prepared HTTPS
files and shell API testing now depend on [native HTTP requests](native-http-requests.md);
then the Lua harness, Hax after). Each delivery task
needs a separate go-ahead.

[Developer tools](later-os-directions.md#developer-tools): a capability inspector
and `top`.

[Git on Pyxis](git-on-pyxis.md): libc source-fetch prerequisites and the native
CLI/HTTP investigation (#711) are merged. Port tasks are proposed, not assigned.

[Network kernel debugger](network-debugger.md): tasks 1–3 are implemented and
natively qualified for read-only LAN attach on the RTL8111
([reference](../kernel/network-debugger.md)); task 4 is unassigned.

[NVMe coexistence](nvme-coexistence.md): task 1's NVMe driver is merged (#710) and
was qualified natively read-only on 2026-10-11. The shared
[FAT32 backend and workflow](../devices/fat32-volumes.md) are delivered; coexistence
integration still needs assignment.

[PDCurses](application-ports.md#libraries-and-terminal-tools) over libterm with one
terminal application, and [SQLite](application-ports.md#libraries-and-terminal-tools)
through a native VFS.

Physical GPU drivers, after the [display milestone](../kernel/display.md): the owner
prepares the hardware.

[Power and ACPI follow-ups](later-os-directions.md#power-and-acpi).

[Remote desktop](remote-desktop.md): a view-only RFB server over screen capture
first, then remote input after the system pointer.

[SMP follow-ups](scheduling-and-threads.md), including serial services off the BSP.

[Steam Deck second hardware target](steam-deck.md): accepted PXE development
direction; the owner's dock firmware PXE check passed. No implementation task is assigned.

System layout follow-ups: network configuration on the pool instead of the archive
([technical debt](../technical-debt.md#archive-only-network-configuration)) and the
[boot configuration checker](boot-configuration-checker.md).

[Terminal applications](terminal-applications.md): a single-panel file navigator,
then operations between navigators.

## Directions

Parked directions and investigations; promote one to a milestone when its
prerequisites and result are clear.

[Application port candidates](application-ports.md), including SDL2/GrafX2,
Peanut-GB, Chocolate Duke3D, DevilutionX and a wasm3 investigation.

[Bluetooth mouse](bluetooth-mouse.md): accepted kernel HCI and userspace stack
direction with explicit Secure Connections enrollment and cold/native closure;
tasks 1 to 3 are complete (#545, #552, #564).

[Building software on Pyxis](source-builds.md): after hosted Clang, ports and
non-rescue userland build from source on the installed system until a package
manager exists.

[Capability-valued environment variables](capability-environment.md): parked
(2026-10-11); revisit before prepared HTTP requests, session configuration or
users and authority store a capability in a variable.

[Control, events and faults](control-events-faults.md): Pyxis's answer to signals,
tied to Continuum.

[Desktop and graphics](desktop-graphics.md), including where to start on the
owner's compositor.

[Hosted toolchains and language runtimes](toolchains-and-runtimes.md), the
[Go runtime investigation](go-runtime.md) and the
[Neovim milestone](neovim-libuv.md): tasks 1–6 delivered, task 7 (owner native
qualification) open.

[Interchangeable providers](interchangeable-providers.md): programs depend on
protocols and launchers choose who serves them, so a compositor can serve the same
display, pointer and keyboard interfaces as the kernel.

[Later OS directions](later-os-directions.md): execution lifecycle, backend
interfaces, networking, device ownership, developer tools, audio, Bluetooth, power,
storage.

[Program bundles and ZIP archives](program-bundles.md): ZIP 2b/2c parked by the
owner on 2026-10-11; `.pxb` meets current needs. Stored launch is implemented and
qualified on draft #717/#213/#98, with authority and admission fixes required
before any merge. Revisit for package distribution or the Clang build loop.
The merged host packer/checker (2a, #706) stays; bin placement is merged
([reference](../userland/program-bundles.md)) and its
[future command index](program-bundles.md#future-command-index) awaits a separate go.

[Selecting existing build artifacts](build-artifact-reuse.md) and the postponed
[host development overlay](host-development-overlay.md).

[Spaces](spaces.md), including [Asterism](spaces.md#asterism), and
[filesystems and namespaces](vfs.md): working drafts.

[Subsystems per space](subsystems-per-space.md): parked (2026-10-13), a userspace
personality such as NT per space; revisit before a second loader or format.

[Users and authority](users-and-authority.md), a cross-cutting design checkpoint,
and [credentials and biometric unlock](credentials-and-biometrics.md), parked after
local users.

[Userspace scheme providers](userspace-scheme-providers.md).

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
