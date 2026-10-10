# Development milestone index

Status: the current state of planned work. Nothing here authorizes
implementation: each milestone settles its decisions in its own document before
code work starts. Milestone order is flexible; the owner's current choice wins.
Everyday use for simple tasks guides the order; website hosting is one future
application, not the completion target. Fixes for major problems found on the
ThinkPad remain allowed in any track.

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

[Bundles in bin://](bundles-in-bin.md): Codex epsilon (2026-10-10), accepted
same-name lookup and relocation of the editor/interpreter bundles; task 1 starts
after the owner merges #686.

[Claude on Pyxis proposal](claude-on-pyxis.md): Codex epsilon (2026-10-09),
prepared HTTPS files and shell API testing, then the Lua harness; Hax after. Docs
only; ZIP defaults accepted in #639, delivery tasks need separate go-aheads.

Developing inside Pyxis, the Java virtual machine: the owner, see
[developing inside Pyxis](in-pyxis-development.md).

[MX Master 3S milestone proposal](bluetooth-mouse.md): Codex (2026-10-08). Tasks 1
to 3 are complete (#545, #552, #564); the [task 1 decision rounds](bluetooth-task1-contracts.md)
are accepted and the [warm qualification](../development/experiments/bluetooth-runtime-hci/README.md)
is recorded. Service and connection traffic is task 4's qualification gate; later
tasks need explicit assignment.

[Name limits and links](../userland/libc-portability.md#name-limits-and-symbolic-links):
Claude (2026-10-10), `NAME_MAX`, O_NOFOLLOW, ELOOP and ENAMETOOLONG from new kernel
statuses.

[NVMe coexistence proposal](nvme-coexistence.md): Claude (2026-10-10), Pyxis beside
Fedora on the internal disk with Limine replacing GRUB; docs only, decisions
accepted, no task started.

[Renoir hardware cursor](renoir-hardware-cursor.md): Codex alpha (2026-10-10), task 2
waits for the owner's Fedora evidence; [whole-frame skipping](../kernel/presenter-frame-skipping.md)
is merged.

[Shell reverse history search](../userland/shell.md#commands-and-quoting): Codex
alpha (2026-10-10), owner-assigned Ctrl+R in the shared line editor.

[Threads task 3](threads.md#shared-capability-and-vm-ownership): Codex delta
(2026-10-10), shared-table delivery, growth, reservations and owned inputs; still
one user task. Tasks 1 and 2 are merged; later gates need separate assignment.

[ThinkPad pointer input quality](pointer-input-quality.md): Codex (2026-10-10),
accepted USB report mice and Synaptics absolute mode and palm-rejection plan;
implementation tasks need a separate go.

## Open

[Developing inside Pyxis](in-pyxis-development.md): the extra-spaces check on the
installed ThinkPad is open.

[Everyday gaps](everyday-gaps.md): small things noticed in use.

[File transfer through the remote terminal](remote-file-transfer.md): drag-and-drop
upload awaits the owner's GUI-drop check from Linux and macOS.

## Candidates for the next milestone

Other candidates; current assignments are listed above.

[Audio consumers](later-os-directions.md#audio): SDL2 and Quake adapters after the
completed analog playback milestone.

Clang running on Pyxis: the [third LLVM milestone](toolchains-and-runtimes.md#llvmclang-transition-and-hosting),
now that [C++ in userspace](../development/cxx-userspace.md) is complete.

[Developer tools](later-os-directions.md#developer-tools): a capability inspector
and `top`.

[Network kernel debugger](network-debugger.md): checkpoint and guarded inspection
implemented; task 3 assigned for both NICs, bridge and native read-only LAN attach
during PXE bring-up.

[PDCurses](application-ports.md#libraries-and-terminal-tools) over libterm with one
terminal application, and [SQLite](application-ports.md#libraries-and-terminal-tools)
through a native VFS.

Physical GPU drivers, after the [display milestone](../kernel/display.md): the owner
prepares the hardware.

[Power and ACPI follow-ups](later-os-directions.md#power-and-acpi).

[Remote desktop](remote-desktop.md): a view-only RFB server over screen capture
first, then remote input after the system pointer.

[SMP follow-ups](scheduling-and-threads.md), including serial services off the BSP.

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
tasks 1 and 2 are complete.

[Building software on Pyxis](source-builds.md): after hosted Clang, ports and
non-rescue userland build from source on the installed system until a package
manager exists.

[Control, events and faults](control-events-faults.md): Pyxis's answer to signals,
tied to Continuum.

[Desktop and graphics](desktop-graphics.md), including where to start on the
owner's compositor.

[Hosted toolchains and language runtimes](toolchains-and-runtimes.md), the
[Go runtime investigation](go-runtime.md) and the
[Neovim milestone proposal](neovim-libuv.md) with its libuv re-check.

[Interchangeable providers](interchangeable-providers.md): programs depend on
protocols and launchers choose who serves them, so a compositor can serve the same
display, pointer and keyboard interfaces as the kernel.

[Later OS directions](later-os-directions.md): execution lifecycle, backend
interfaces, networking, device ownership, developer tools, audio, Bluetooth, clock
source, power, storage.

[Selecting existing build artifacts](build-artifact-reuse.md) and the postponed
[host development overlay](host-development-overlay.md).

[Spaces](spaces.md), including [Asterism](spaces.md#asterism), and
[filesystems and namespaces](vfs.md): working drafts.

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
