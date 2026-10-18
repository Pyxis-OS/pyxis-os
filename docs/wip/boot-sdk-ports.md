# Development milestone index

Status: the current state of planned work. Nothing here authorizes
implementation: each milestone settles its decisions in its own document before
code work starts. Milestone order is flexible; the owner's current choice wins.
Everyday use for simple tasks guides the order; website hosting is one future
application, not the completion target. Fixes for major problems found on the
ThinkPad remain allowed in any track.

**Planning freeze lifted (owner, 2026-10-17):** proposals and investigations for
concrete problems are allowed again. Larger new milestones still wait until the
owner starts that planning; ideas keep going in
[later OS directions](later-os-directions.md).

**Sequencing (owner, 2026-10-14):** the [capability model](capabilities-and-providers.md)
is settled and its [implementation](capability-model-implementation.md) is assigned.
Libc, shell, web/HTTP and Git work happens only through that milestone until the
task owning the area is delivered; independent tracks (storage and drivers,
kernel) continue alongside.

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

[Capability model implementation](capability-model-implementation.md): owner-assigned
2026-10-14; Codex beta owns integration, with Codex delta on the kernel tasks.
A (core) and [B (HTTP)](../interfaces/http-transactions.md) are complete;
C (general capability pipes) remains planned.

[BSP pinning inventory](bsp-pinning-inventory.md): owner-requested freeze exception,
assigned to Codex alpha 2026-10-13; read-only ownership/latency inventory and
first-slice proposal, coordinated with Claude's bounded audio/teardown work.

Developing inside Pyxis, the Java virtual machine: the owner, see
[developing inside Pyxis](in-pyxis-development.md).

[Explicit durability](explicit-durability.md): planned 2026-10-17, three
decisions accepted; starts after clangd. npfs namespace operations join the
running journal transaction, then the FAT32 writer batches its ordered phases.

[FAT32 mutation preflight cost](fat32-preflight-cost.md): owner-assigned
investigation (2026-10-13) before installed configuration publication; whole-volume
read/workspace costs are proposed for reduction without extending the lease.

[Installer TUI and configuration delta editor](installer-tui.md): tasks 1–4
are delivered ([#786](https://git.internal/PyxisOS/pyxis-os/pulls/786)). Task 5's
publication/installed-activation proposal is assigned to Codex alpha; owner
decisions are pending. Activation waits for FAT32 preflight cost, and internal
coexistence also needs NVMe task 3. Later tasks need separate go-aheads.

[Multiple network interfaces](network-interfaces.md): owner-assigned to Codex
epsilon (2026-10-18), task 1 contracts/baseline delivered. Interface-owned core
is next, pending its own go; prerequisite for Wi-Fi stage 3.

[NVMe coexistence](nvme-coexistence.md): tasks 1–3 delivered (task 3 in
[#878](https://git.internal/PyxisOS/pyxis-os/pulls/878), 2026-10-16); native writes stay
disabled. The first native install waits for alpha's publisher and sideloads Limine
on the ThinkPad.

[PCI BAR assignment](pci-bar-assignment.md): owner-requested scope assigned to
Codex delta (2026-10-15), claimed-device memory BAR placement from ACPI root
windows, with size/alignment and collision proof. Vela's touchpad is the first
consumer; implementation awaits the proposed decisions.

[Renoir hardware cursor](renoir-hardware-cursor.md): Codex alpha (2026-10-10), the
Fedora evidence was captured and analysed on 2026-10-11 (#685, draft). Task 2 is
blocked on hardware semantics, not on evidence;
[whole-frame skipping](../kernel/presenter-frame-skipping.md) is merged.

[Shell conditions, merged output and script runtime](shell-gaps.md): owner-assigned
2026-10-18; all three decisions accepted. Conditions/exit first, runtime-only
script re-entry next, then ordered `2>&1` and Neovim collection options.

[USB NCM receive throughput](usb-ncm-throughput.md): owner-assigned to Codex
epsilon (2026-10-18), RX handoff, backlog sizing and TX-impact qualification on
the ThinkPad's RTL8156BG.

[Vela I2C touchpad](vela-i2c-touchpad.md): owner-assigned to Codex delta
(2026-10-15): tasks 1–2 are native-qualified; task 3 HID-over-I2C polling
is assigned (2026-10-16), with native input/rate qualification pending.
then precision contacts through the existing touchpad filter and GPIO delivery.
Layers remain reusable for the Deck; its milestone stays on hold.

[Packages and upgrades](packages.md): owner-requested plan (2026-10-18), three
decisions accepted; data packages first, then program packages per release,
native links and dynamic linking. Hard links are a separate prerequisite.

[Wi-Fi](wifi.md): owner-requested investigation/proposal (2026-10-18),
stages and three defaults accepted; AX200 first, RTL8922AE second, Deck deferred.
Codex beta owns stage 1 now that the multiple-interface plan is accepted; its
implementation precedes Wi-Fi stage 3.

## Open

[Developing inside Pyxis](in-pyxis-development.md): the extra-spaces check on the
installed ThinkPad is open.

[Everyday gaps](everyday-gaps.md): small things noticed in use.

## Candidates for the next milestone

Other candidates; current assignments are listed above.

[Audio follow-ups](later-os-directions.md#audio): analog playback and SDL2/Quake
adapters are delivered; [SDL latency tuning](../technical-debt.md#sdl-audio-latency) remains deferred.

[Claude on Pyxis](claude-on-pyxis.md): design accepted 2026-10-09; the Lua harness
consumes the delivered [HTTP resources](../userland/http-fetch.md), with Hax later.
Each delivery task needs a separate go-ahead.

[Developer tools](later-os-directions.md#developer-tools): a capability inspector,
`top` and the network debugger's unassigned task 4.

[Git on Pyxis](git-on-pyxis.md): libc source-fetch prerequisites and the native
CLI/HTTP investigation (#711) are merged. Port tasks are proposed, not assigned.

[PDCurses](application-ports.md#libraries-and-terminal-tools) over libterm with one
terminal application, and [SQLite](application-ports.md#libraries-and-terminal-tools)
through a native VFS.

Physical GPU drivers, after the [display milestone](../kernel/display.md): the owner
prepares the hardware.

[Power and ACPI follow-ups](later-os-directions.md#power-and-acpi).

[Print Screen key](print-screen.md): implemented for review, 2026-10-18; a
system binding saves the whole shown screen to `home://screenshots/` with the
space name and ISO date and time.

[Remote desktop](remote-desktop.md): a view-only RFB server over screen capture
first, then remote input after the system pointer.

[SMP follow-ups](scheduling-and-threads.md), including serial services off the BSP.

[Steam Deck second hardware target](steam-deck.md): accepted PXE development
direction, on hold by owner direction (2026-10-15). Task 9 dock Ethernet is
merged; no task 2/3 scope or implementation is assigned. Reusable I2C work is
assigned separately to [vela's touchpad](vela-i2c-touchpad.md).

System layout follow-ups: network configuration on the pool instead of the archive
([technical debt](../technical-debt.md#archive-only-network-configuration)) and the
[boot configuration checker](installer-tui.md#configuration-checker).

[Terminal applications](terminal-applications.md): a single-panel file navigator,
then operations between navigators.

[Userland fonts](userland-fonts.md): planned 2026-10-17, decisions accepted;
`libfont` over stb_truetype with CaskaydiaCove Nerd Font, IBM Plex Sans and IBM
Plex Serif under `boot://share/fonts/`, usable from hosted Clang.

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

[Capabilities and providers](capabilities-and-providers.md): the capability model,
settled with the owner on 2026-10-14: typed capabilities instead of URI spellings,
universal operations, one environment, revocation, facets and retention. Its
provider-side questions remain open; its
[implementation](capability-model-implementation.md) is assigned.

[Control, events and faults](control-events-faults.md): Pyxis's answer to signals,
tied to Continuum.

[Desktop and graphics](desktop-graphics.md), including where to start on the
owner's compositor.

[Host development overlay](host-development-overlay.md): postponed experiment.

[Hosted toolchains and language runtimes](toolchains-and-runtimes.md) and the
[Go runtime investigation](go-runtime.md).

[Later OS directions](later-os-directions.md): execution lifecycle, backend
interfaces, networking, device ownership, developer tools, audio, Bluetooth, power,
storage.

[Nested execution groups](nested-execution-groups.md): the minimum subgroup
foundation is delivered ([#789](https://git.internal/PyxisOS/pyxis-os/pulls/789));
the broader required direction is parked.

[Program bundles and ZIP archives](program-bundles.md): ZIP 2b/2c parked by the
owner on 2026-10-11; `.pxb` meets current needs. Stored launch is implemented and
qualified on draft #717/#213/#98, with authority and admission fixes required
before any merge. Revisit for package distribution or the Clang build loop.
The merged host packer/checker (2a, #706) stays; bin placement is merged
([reference](../userland/program-bundles.md)) and its
[future command index](program-bundles.md#future-command-index) awaits a separate go.

[Spaces](spaces.md), including [Asterism](spaces.md#asterism), and
[filesystems and namespaces](vfs.md): working drafts.

[Subsystems per space](subsystems-per-space.md): parked (2026-10-13), a userspace
personality such as NT per space; revisit before a second loader or format.

[Users and authority](users-and-authority.md), a cross-cutting design checkpoint,
and [credentials and biometric unlock](credentials-and-biometrics.md), parked after
local users.

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
