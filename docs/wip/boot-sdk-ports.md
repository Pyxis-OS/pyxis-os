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

Developing inside Pyxis, the Java virtual machine: the owner, see
[developing inside Pyxis](in-pyxis-development.md).

[Git on Pyxis](git-on-pyxis.md): the libc prerequisites of the libgit2 source-fetch
route are merged. A docs-only investigation of a native Git CLI and HTTP transport
is in review (#711); no Git port task is assigned.

[Hosted Clang and LLD](hosted-clang.md): Codex (2026-10-11), accepted first
single-source compile and separate link. Support task 1 is delivered in the fork
(#708), without adopting its pin; SDK/libc tasks need separate assignments and proceed
alongside delta's native threads gates 5–8. No runnable hosted compiler yet.

[Installer TUI and configuration delta editor](installer-tui.md): Codex
(2026-10-11), PDCurses pages over a shared plan/apply executor, revision
inventories, mounted read-write FAT32 and owned ESP configuration reconciliation.
The contract is accepted; task 1 is held and later tasks need separate assignments.

[Native command execution](native-command-execution.md): Codex delta (2026-10-11),
a shared proposal for selected native streams, capture/deadlines and shell/Lua
pipelines, extracted from Claude/Git/ports/libuv. Direct launch without descendants
is accepted; remaining decisions and every implementation task need a separate go; FAT32 proposal follows after review.

[Native HTTP requests](native-http-requests.md): Codex delta (2026-10-11), a
shared plan accepted by the owner, extracted from Claude/Git prerequisites:
prepared POST, streamed GET/POST without an artificial response-size cap, and
shell/Lua delivery.
No implementation task is assigned; the command-execution proposal is separate.

[Remote multiplexer](remote-multiplexer.md): Codex epsilon (2026-10-11), accepted
guest VT composition with trusted remote pane creation and cleanup supervision.
Tasks 1–2 are assigned together, to start after the plan merges; live resize
(task 3) needs a separate go.

[Renoir hardware cursor](renoir-hardware-cursor.md): Codex alpha (2026-10-10), the
Fedora evidence was captured and analysed on 2026-10-11 (#685, draft). Task 2 is
blocked on hardware semantics, not on evidence;
[whole-frame skipping](../kernel/presenter-frame-skipping.md) is merged.

[Steam Deck second hardware target](steam-deck.md): the owner (2026-10-11), plan
accepted with a PXE development loop. The owner's firmware PXE check passed through
the dock on 2026-10-11 (dock keyboard and mouse work; the internal xHCI `04:00.4`
with the Steam Controller timed out). No task is assigned.

[ThinkPad pointer input quality](pointer-input-quality.md): Codex alpha
(2026-10-11), accepted USB report mice and Synaptics absolute mode and
palm-rejection plan. Task 1 is assigned, with task 2 to follow.

[Threads gates 5–8](threads.md#delivery-gates): Codex delta (2026-10-11), native
lifecycle, parking, TLS and libc delivery. Tasks 1–4 are merged and task 5 is in
review; each later task retains its separate owner-assignment gate.

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

[Claude on Pyxis](claude-on-pyxis.md): design accepted 2026-10-09 (prepared HTTPS
files and shell API testing now depend on [native HTTP requests](native-http-requests.md);
then the Lua harness, Hax after). Each delivery task
needs a separate go-ahead.

[Developer tools](later-os-directions.md#developer-tools): a capability inspector
and `top`.

[Network kernel debugger](network-debugger.md): checkpoint and guarded inspection
implemented and natively qualified for read-only LAN attach on the RTL8111
(tasks 1–3); task 4 is unassigned.

[NVMe coexistence](nvme-coexistence.md): task 1's NVMe driver is merged (#710) and
was qualified natively read-only on 2026-10-11; the FAT32 and installer coexistence
tasks need assignment.

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
[Neovim milestone proposal](neovim-libuv.md) with its libuv re-check.

[Interchangeable providers](interchangeable-providers.md): programs depend on
protocols and launchers choose who serves them, so a compositor can serve the same
display, pointer and keyboard interfaces as the kernel.

[Later OS directions](later-os-directions.md): execution lifecycle, backend
interfaces, networking, device ownership, developer tools, audio, Bluetooth, clock
source, power, storage.

[Nested execution groups](nested-execution-groups.md): parked REQUIRED direction
(2026-10-11), contained subgroups and default child launchers before Git descendant
helpers, driver-launched Clang tools, native make/ports builds and libuv/Neovim
descendant jobs. No design/API or implementation is assigned.

[Program bundles and ZIP archives](program-bundles.md): ZIP 2b/2c parked by the
owner on 2026-10-11; `.pxb` meets current needs. Stored launch is implemented and
qualified on draft #717/#213/#98, with authority and admission fixes required
before any merge. Revisit for package distribution or the Clang build loop.
The merged host packer/checker (2a, #706) stays; [bin placement](bundles-in-bin.md)
is merged and its future command index awaits a separate go.

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
