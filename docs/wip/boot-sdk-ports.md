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

**Releases (owner, 2026-10-20):** 0.0.7 is released when the work in flight since
0.0.6 reaches its milestone boundaries, never by date: [packages slice 1](packages.md#slice-1-data-packages)
closed; [explicit durability](../devices/explicit-durability.md) closed, including its
task 4 native measurement (2026-10-20); [Wi-Fi](wifi.md) stage 3 closed, a stable
2.4 GHz open-AP connection with data; the [capability pipes](capability-model-implementation.md#task-9-first-slice-decisions-accepted-owner-2026-10-20)
first slice (tasks 1-3) merged; and a clean native install and update on the
green reference stick, the [installer](installer-tui.md) task 5 gate
(owner-run vela pass, 2026-10-20). 0.0.8 brings
the [public package mirror](packages.md#later-ideas) (`pkg.pyxis.nexus`, with retention and index
and package signing), the first tester ISO, a short technical manual, and the
[project website](website.md) at `pyxis.nexus` (domain registered 2026-10-20;
chronium, 2026-10-20). Tags and history are in [releases](../releases.md). 0.1.0 direction (owner, 2026-10-20): Pyxis manages its own disks and the live image is the installer ([disk management](release-ideas.md#1-disk-management-the-live-image-is-the-installer)); more items may join.

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

[Bulk FILE reads](bulk-file-reads.md): accepted and assigned (chronium, 2026-10-25),
with bounded provider/npfs reads preserving authority and owned lifetimes.
The 0.0.9 release merge gate is lifted; new consumers need bulk-capable kernels.

[Capability model implementation](capability-model-implementation.md): owner-assigned
2026-10-14; Codex beta owns integration, with Codex delta on the kernel tasks.
A (core) and [B (HTTP)](../interfaces/http-transactions.md) are complete;
C (general capability pipes) remains planned.

[Controller support](controller-support.md): Edge-only wired input assigned to
Codex delta (chronium, 2026-10-26); descriptor-checked dedicated decoder, native
keyboard-style gamepad authority/focus and Pyxis SDL backend. Task 0 closes the
actual-device contract before task 1. Ultimate 2 and generic HID are later.

Developing inside Pyxis, the Java virtual machine: the owner, see
[developing inside Pyxis](in-pyxis-development.md).

[FAT32 mutation preflight cost](fat32-preflight-cost.md): owner-assigned
investigation (2026-10-13) before installed configuration publication; whole-volume
read/workspace costs are proposed for reduction without extending the lease.

[Git](../userland/git.md): the three CLI slices are implemented (chronium,
2026-10-24): HTTPS clone/push/pull, init/remotes, local staging/commit/diff and
held-root identity/credentials. QEMU Forgejo qualification and host/nested clone
measurements are recorded; native media cost and credential-manager work remain
outside this milestone.

[Go runtime investigation](go-runtime.md): chronium-assigned to Codex alpha,
2026-10-21. Fresh source audit and proposal; fast startup is required. No
implementation is assigned.

[Hosted build flags](hosted-build-flags.md): chronium-assigned investigation
(2026-10-25) for 0.0.10: hosted C boundaries, explicit Mesa optimization and the
userspace red-zone convention. Measured prototype and decisions precede SDK defaults.

[Installer TUI and configuration delta editor](installer-tui.md): tasks 1–4
are delivered ([#786](https://git.internal/PyxisOS/pyxis-os/pulls/786)). Task 5
[#797](https://git.internal/PyxisOS/pyxis-os/pulls/797) has the owner-run Classic
native gate; corrected action/completion and owned Coexist Install/Update have
QEMU qualification. The current publication and package-set behavior is in
[package upgrades](../userland/package-upgrades.md); native Coexist firmware
and release-upgrade checks remain [open](https://git.internal/PyxisOS/pyxis-os/issues/1509).
Later tasks need separate go-aheads.

[NVMe coexistence](nvme-coexistence.md): tasks 1–3 delivered (task 3 in
[#878](https://git.internal/PyxisOS/pyxis-os/pulls/878), 2026-10-16). The
[GRUB → Limine milestone](../userland/grub-limine-chainload.md) is complete:
ThinkPad manual Coexist Install/Update and normal/rescue boots passed
[native qualification](../development/experiments/grub-limine-native.md),
2026-10-21. Owned Coexist publication is now implemented and QEMU-qualified
through [package upgrades](../userland/package-upgrades.md); vela's native
firmware gate remains [open](https://git.internal/PyxisOS/pyxis-os/issues/1509).

[Renoir hardware cursor](renoir-hardware-cursor.md): Codex alpha (2026-10-10), the
Fedora evidence was captured and analysed on 2026-10-11 (#685, draft). Task 2 is
blocked on hardware semantics, not on evidence;
[whole-frame skipping](../kernel/presenter-frame-skipping.md) is merged.

[Rust](rust.md): stage 1 core+alloc/tiny-skia delivered in
[#1224](https://git.internal/PyxisOS/pyxis-os/pulls/1224).
[Rust std](rust-std.md): stage-2 implementation assigned to Codex epsilon
(chronium, 2026-10-25); fork compiler, std main and the read-only-file hexyl
subset are delivered with recorded QEMU qualification. Full std profile, Rust
workers and native hardware qualification remain open; hosted Rust remains later.

[Script launch authority](../userland/script-authority.md): implemented for the
exact held shell and standalone Lua 5.5/5.1, with shared source selection and
shell-equivalent lifetime. Embeddings and captured jobs retain their profiles.

[USB NCM receive throughput](usb-ncm-throughput.md): owner-assigned to Codex
epsilon (2026-10-18). The retained RX handoff is merged with the 16-frame queue
kept, and the always-drain fix (#1008) is merged; the matched TX A/B waits for
the owner's run on the ThinkPad's RTL8156BG.

[Vela I2C touchpad](vela-i2c-touchpad.md): owner-assigned to Codex delta
(2026-10-15): tasks 1–4 are merged and native-qualified (#894, #904, #910);
task 5, GPIO interrupt delivery, needs its own go. Layers remain reusable for
the Deck; its milestone stays on hold.

[Packages and upgrades](packages.md): data and program packages, the installed
command/SDK views, core archive and online Install are delivered. The
[0.0.10 package and release upgrades](../userland/package-upgrades.md) are
implemented: whole-set updates, one active release, verified first-boot set
selection, owned Classic/Coexist publication and reviewed signed-index floors.
The release-day vela native gate remains [open](https://git.internal/PyxisOS/pyxis-os/issues/1509).
The earlier [updater record](pkg-upgrade.md) is historical; dynamic linking
and on-device source builds remain later work.

[Wi-Fi](wifi.md): stages 1–4 complete; the [AX200 reference](../devices/ax200-wifi.md)
covers firmware, passive scan, legacy open and WPA2-CCMP links with independent
DHCP/verified data and recorded qualification limits. Next: vela RTL8922AE, then
WPA3. AX200 5 GHz and the Deck remain deferred.

## Open

[Developing inside Pyxis](in-pyxis-development.md): the extra-spaces check on the
installed ThinkPad is open.

[Everyday gaps](everyday-gaps.md): small things noticed in use.

## Planned next kernel milestone

[Remove BSP-owned allocation and VM mutation](bsp-removal.md): tasks 1–3 and the
interposed JIT grant are merged. Tasks 4–5 are assigned to Codex beta (chronium,
2026-10-28): task 4's cleanup-placement/lifetime decisions are accepted; the
remaining VM-client audit follows. BSP removal remains active; no retained
per-CPU frame cache or device-worker migration is authorized.

## Candidates for the next milestone

Other candidates; current assignments are listed above.

Two candidates come first, in priority order (chronium, 2026-10-23); the rest are
alphabetical.

1. [Command-resolution service](later-os-directions.md#command-resolution-service):
   libc's command-selection policy behind a typed, restartable userland service
   shared by libc and Go. Higher priority than the next one.
2. [Go cgo](go-runtime.md#cgo-for-go-programs-that-wrap-c-libraries--candidate-chronium-2026-10-23):
   Go programs that wrap C libraries, with external linking through LLD and the
   Go runtime and libc in one process.

[Audio follow-ups](later-os-directions.md#audio): analog playback and SDL2/Quake
adapters are delivered; [SDL latency tuning](https://git.internal/PyxisOS/pyxis-os/issues/1397) remains deferred.

[Claude on Pyxis](claude-on-pyxis.md): design accepted 2026-10-09; the Lua harness
consumes the delivered [HTTP resources](../userland/http-fetch.md), with Hax later.
Each delivery task needs a separate go-ahead.

[Developer tools](later-os-directions.md#developer-tools): a capability inspector,
`top` and the network debugger's unassigned task 4.

[Raw terminals and curses applications](terminal-ui.md): native raw access,
ncursesw with `TERM=pyxis`, then hexcurse (0.0.9; assigned, delta).
[SQLite](application-ports.md#libraries-and-terminal-tools) through a native VFS.

[Intel inherited scanout](intel-inherited-scanout.md): vela tearing investigation
assigned to Codex alpha (chronium, 2026-10-28); ordinary inherited-state startup/readout accepted; plane/GGTT writes await evidence.
Broader physical GPU drivers remain separately assigned.

[Power and ACPI follow-ups](later-os-directions.md#power-and-acpi).

[Remote desktop](remote-desktop.md): a view-only RFB server over screen capture
first, then remote input after the system pointer.

[SMP follow-ups](scheduling-and-threads.md), including serial services off the BSP.

[Steam Deck second hardware target](steam-deck.md): accepted PXE development
direction, on hold by owner direction (2026-10-15). Task 9 dock Ethernet is
merged; no task 2/3 scope or implementation is assigned. Reusable I2C work is
assigned separately to [vela's touchpad](vela-i2c-touchpad.md).

System layout follow-up: the
[boot configuration checker](installer-tui.md#configuration-checker).

[Terminal applications](terminal-applications.md): a single-panel file navigator,
then operations between navigators.

## Directions

Parked directions and investigations; promote one to a milestone when its
prerequisites and result are clear.

[Application port candidates](application-ports.md), including SDL2/GrafX2,
Peanut-GB, Chocolate Duke3D, DevilutionX and a wasm3 investigation.

[Ship of Harkinian](ship-of-harkinian.md): tasks 1–6 deliver reusable libraries,
private extraction, first frame and QEMU keyboard gameplay/audio/save checks.
Partial vela play/save/reload/quit passed; native resource qualification remains
open, and the same-boot RAM save check does not establish reboot durability.

[Bluetooth mouse](bluetooth-mouse.md): accepted kernel HCI and userspace stack
direction with explicit Secure Connections enrollment and cold/native closure;
tasks 1 to 3 are complete (#545, #552, #564).

[Building software on Pyxis](source-builds.md): a separate future workflow for
running maintained recipes on the installed system and producing package
outputs. Binary program packages and Git already exist; native recipe execution
and build-output publication still need their own proposal.

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

[Hosted toolchains and language runtimes](toolchains-and-runtimes.md).

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

[Release ideas](release-ideas.md): ideas and directions collected for 0.1.0 and
later releases, each with its status and target; the first is disk management.

[Ready-to-run QEMU image](qemu-image.md): a qcow2 with the release installed, built in the
release flow beside the ISO; plan accepted in part (2026-10-29), tasks not started.

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
in Git history. Carry forward relevant deferred work into another WIP document
or a `debt` issue. Do not retain a duplicate archive of the old plan.
