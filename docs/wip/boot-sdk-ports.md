# Development milestone index

Status: the current state of planned work. Nothing here authorizes
implementation: each milestone settles its decisions in its own document before
code work starts. Milestone order is flexible; the owner's current choice wins.
Everyday use for simple tasks guides the order; website hosting is one future
application, not the completion target. Fixes for major problems found on the
ThinkPad remain allowed in any track.

Release history and publication state live in [releases](../releases.md).

Implemented behavior lives in the subsystem references listed in the
[documentation index](../README.md). Completed milestones keep no WIP document
and are listed in [completed milestones](../development/completed-milestones.md).

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
C (general capability pipes) remains assigned to Codex delta.

[Dynamic linking](dynamic-linking.md): claude's design, accepted by chronium
(2026-11-02): P2F/P1D/P2D formats, a kernel map-and-seal operation with a
userspace loader, `.pxl` libraries exported by packages, and the build split
that removes pyxis-os's dependency on userland and ports builds. Release
placement is open; no task has started.

[Intel eDP through IEGD](intel-iegd.md): alpha owns the native kernel/OAL port and
real vela panel ownership, accepted by chronium (2026-11-02). Inherited-flip and
two-plane implementation are parked; GuC rendering follows later.

[Wi-Fi](wifi.md): stages 1–4 complete; the [AX200 reference](../devices/ax200-wifi.md)
covers firmware, passive scan, legacy open and WPA2-CCMP links with independent
DHCP/verified data and recorded qualification limits. Next: vela RTL8922AE, then
WPA3. AX200 5 GHz and the Deck remain deferred.

## Open

[Everyday gaps](everyday-gaps.md): small things noticed in use.

## Candidates for the next milestone

Other candidates; current assignments are listed above.

Two candidates come first, in priority order (chronium, 2026-10-23); the rest are
alphabetical.

1. [Command-resolution service](later-os-directions.md#command-resolution-service):
   libc's command-selection policy behind a typed, restartable userland service
   shared by libc and Go. Higher priority than the next one.
2. [Go cgo](https://git.internal/PyxisOS/pyxis-os/issues/1690):
   Go programs that wrap C libraries, with external linking through LLD and the
   Go runtime and libc in one process.

[Audio follow-ups](later-os-directions.md#audio): analog playback and SDL2/Quake
adapters are delivered; [SDL latency tuning](https://git.internal/PyxisOS/pyxis-os/issues/1397) remains deferred.

[Claude on Pyxis](claude-on-pyxis.md): design accepted 2026-10-09; the Lua harness
consumes the delivered [HTTP resources](../userland/http-fetch.md), with Hax later.
Each delivery task needs a separate go-ahead.

[Developer tools](later-os-directions.md#developer-tools): a capability inspector,
`top` and the network debugger's unassigned task 4.

[SQLite](application-ports.md#libraries-and-terminal-tools) through a native VFS.


[Installer curses frontend](https://git.internal/PyxisOS/pyxis-os/issues/1686): unassigned UI/Wi-Fi join
follow-up over the delivered planner and executor.

[Power and ACPI follow-ups](later-os-directions.md#power-and-acpi).

[Remote desktop](remote-desktop.md): a view-only RFB server over screen capture
first, then remote input after the system pointer.

[Rust follow-ups](rust.md): hosted rustc/Cargo, later ports, unwinding and tier-3
upstreaming. The [implemented first profile](../development/rust-toolchain.md#native-std-profile)
has separate [native qualification debt](https://git.internal/PyxisOS/pyxis-os/issues/1679).

[SMP follow-ups](../kernel/smp.md), including serial services off the BSP.

[Steam Deck second hardware target](steam-deck.md): accepted PXE development
direction, on hold by owner direction (2026-10-15). Task 9 dock Ethernet is
merged; no task 2/3 scope or implementation is assigned. Reusable
[HID-over-I2C input](../devices/hid-i2c-touchpad.md) is implemented;
GPIO interrupt delivery remains [separate work](https://git.internal/PyxisOS/pyxis-os/issues/1492).

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
Current bundle placement is documented in the
[reference](../userland/program-bundles.md); the
[future command index](program-bundles.md#future-command-index) awaits a separate go.

[Release ideas](release-ideas.md): ideas and directions collected for 0.1.0 and
later releases, each with its status and target; the first is disk management.

[Renoir hardware cursor](https://git.internal/PyxisOS/pyxis-os/issues/1699): implementation remains
parked pending latch/disable/fetch-retirement proof; only its read-only inventory
is delivered. The [current route](../kernel/renoir-flip.md#hardware-cursor-status)
uses software composition.

[Spaces](spaces.md), including [Asterism](spaces.md#asterism), and
[filesystems and namespaces](vfs.md): working drafts.

[Subsystems per space](subsystems-per-space.md): parked (2026-10-13), a userspace
personality such as NT per space; revisit before a second loader or format.

[Users and authority](users-and-authority.md), a cross-cutting design checkpoint,
and [credentials and biometric unlock](credentials-and-biometrics.md), parked after
local users.

## Boundaries and completion

Repository ownership and build order live in [SDK and repositories](../development/sdk-and-repositories.md).
Follow [AGENTS.md](../../AGENTS.md) for task authorization and close-out: delivered
contracts belong in subject references; residual limitations and native checks
belong in labelled issues. Future directions do not authorize implementation.
