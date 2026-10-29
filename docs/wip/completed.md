# Completed milestones

Completed milestones keep no WIP document: their behavior lives in the linked
references, and open native checks live in [technical debt](https://git.internal/PyxisOS/pyxis-os/issues?type=all&state=all&labels=4).
Entries are one short paragraph each, in alphabetical order. Add an entry only
when a milestone completes, as the [milestone index](boot-sdk-ports.md) says.

[ACPI](../kernel/acpi.md), 2026-10-07: power-off, restart, the power button and
battery.

- [x] [Atomic append](../interfaces/atomic-append.md), 2026-10-28: bounded native
  EOF placement, libc/shell and Go/Rust/LLVM integration; QEMU and native vela RAM
  qualification. [Newer-shell](https://git.internal/PyxisOS/pyxis-os/issues/1537)
  and [persistent-media](https://git.internal/PyxisOS/pyxis-os/issues/1538) native
  checks remain open.

[Automatic wired binding and route preference](../devices/net0-selection.md),
2026-10-19: boot-discovered wired interfaces, exclusions, stable initial roles,
observed link rates and scoped automatic routing/numeric DNS; management and
retained transports stay pinned. [Qualification](../development/experiments/automatic-network-preference.md)
is QEMU-only; native rate/preference checks remain unrun.

[AX200 Bluetooth investigation](../devices/ax200-bluetooth.md), 2026-10-08:
warm-host QEMU passthrough through identification of the MX Master 3S; see the
[final report](../development/bluetooth-investigation.md). Production mouse work
remains in the [mouse milestone](bluetooth-mouse.md).

- [x] [BSP allocation and VM ownership](../kernel/vm-ownership.md), 2026-10-29:
  caller-CPU memory, preparation and DISPLAY mapping work, acknowledged shared
  retirement and fixed per-CPU RAM reapers. Device/control owners remain fixed;
  [qualification gaps](https://git.internal/PyxisOS/pyxis-os/issues/1600),
  [extended races](https://git.internal/PyxisOS/pyxis-os/issues/1601),
  [retained cleanup](https://git.internal/PyxisOS/pyxis-os/issues/1514) and the
  [SEND residual](https://git.internal/PyxisOS/pyxis-os/issues/1588) remain tracked.

[BSP pinning inventory](../wip/scheduling-and-threads.md#serial-services-off-the-bsp),
2026-10-14: its first slice, [bounded process teardown](../development/experiments/bounded-teardown/README.md),
is implemented; fixed worker CPUs are an [SMP rule](../kernel/smp.md#remaining-serial-services-and-limits)
and the remaining ranking is an SMP follow-up.

[C++ in userspace](../development/cxx-userspace.md), 2026-10-08: libc++, libc++abi
and libunwind in the SDK, with fmt as the first C++ port.

[Cheaper timekeeping](../kernel/timekeeping.md#clock-reads-in-timer-handling),
2026-10-10: fewer clock reads per timer event (#571) and the TSC with HPET
fallback (#577), natively qualified; [measurements](../development/experiments/tsc-clock/README.md).

[clangd in Neovim](../userland/clangd.md), 2026-10-18: Neovim jobs, the clangd
bundle, file URIs, the language server setup and full-parse completion,
qualified on vela.

[Clipboard](../interfaces/clipboard.md), 2026-10-09: per-space and shared stores with
terminal and multiplexer Copy and Paste (#619) and the SDL2 text adapter (#644),
with the [first-delivery qualification](../development/clipboard-first-delivery-qualification.md)
and [limits](https://git.internal/PyxisOS/pyxis-os/issues/1395). Typed objects
and converters stay in [later directions](later-os-directions.md#clipboard-typed-objects-and-converters).

[Clock reads](../interfaces/clocks.md), 2026-10-22: interrupt-cached observation
and parking, KVM time with a cross-CPU floor, explicit PRECISE authority and
SDK/runtime/grant integration. [Qualification](../development/experiments/clock-precision.md)
keeps native and nested results separate; no attributable native speedup.

[Command search](../userland/command-search.md), 2026-10-19: inherited ordered
command capabilities, live package-command views and common shell/Lua/libuv/editor
lookup with retained selections. Native build chains remain separately gated.

[Deadline sleep wakeups](../kernel/timekeeping.md), 2026-10-08: per-CPU one-shot
LAPIC deadlines, [checked natively](https://git.internal/PyxisOS/pyxis-os/issues/1379).

[Display drivers and resizing](../kernel/display.md), 2026-10-07.

[Everyday commands](../userland/everyday-commands.md), 2026-10-07.

[Explicit filesystem durability](../devices/explicit-durability.md), 2026-10-20:
npfs namespace/file writeback in the running transaction, ordered FAT32 batches
and explicit caller sync points. Task 4's [native ThinkPad ABBA](../development/experiments/explicit-durability-measurements.md)
passed extraction, staging, verification and cleanup on the 32 GB SanDisk.

[FAT32 volumes](../devices/fat32-volumes.md), 2026-10-13: shared inspection/raw-write
engine, native RO/RW mounts and declared trusted workflow (#740, #747, #755, #764,
#775). [Experiment records](../devices/fat32-volumes.md#qualification-and-remaining-limits)
remain; [hardware qualification](https://git.internal/PyxisOS/pyxis-os/issues/1418),
including the owner-present RO ESP check, is pending.

[Graphics and terminal layers](../userland/space-layers.md), 2026-10-08:
Super+Up/Down, [checked natively](https://git.internal/PyxisOS/pyxis-os/issues/1336).

[GRUB → Limine chainloading](../userland/grub-limine-chainload.md), 2026-10-21:
disposable Fedora/BLS rehearsal and native ThinkPad manual Coexist Install/Update,
normal/rescue boots, Fedora returns and visually confirmed power-off.
[Library development packages](../development/library-dev-packages.md),
2026-10-23: the reusable ports archives/headers and static pkg-config metadata
join `sdk://` through named dev packages; Lua ABIs coexist, with private
supplicant/embedded libraries kept out. [Qualification](../development/experiments/library-dev-packages.md)
is QEMU-only.

[Native qualification](../development/experiments/grub-limine-native.md) records
the private power-helper limit; automatic Coexist publication and vela stay separate.

[HD Audio investigation](../development/audio-investigation.md), 2026-10-08:
QEMU probes that identified the ALC257 and led to the playback milestone below.

[HD Audio playback](../devices/hda.md), 2026-10-09: BSP-owned analog engine and
[raylib graphics and audio](../development/raylib.md), 2026-10-23: static
SDL2/rlsw drawing, declared playback authority, separate SDK-view development
packages and the shapes example. Drawing has QEMU/ThinkPad/vela evidence;
[native audio and wider qualification](https://git.internal/PyxisOS/pyxis-os/issues/1346)
remain recorded limits. [Minesweeper](../userland/minesweeper.md) is the first
public game package on it.

[up to eight PCM sessions](../interfaces/audio.md), one per space. Open debt:
[live jack switching](https://git.internal/PyxisOS/pyxis-os/issues/1477)
and the [nested-QEMU limit](https://git.internal/PyxisOS/pyxis-os/issues/1391).

[Hosted Clang and LLD](../development/hosted-tools.md), 2026-10-13: Clang and LLD
compile, link and run a C program inside Pyxis from pool-volume bundles, and the
driver runs LLD itself (`clang hello.c -o hello.pxe`), natively qualified on the
ThinkPad (#708, #714, #718, #763, #772, #793). Optional native checks and image
reuse remain [technical debt](https://git.internal/PyxisOS/pyxis-os/issues/1384).

[HTTP redirects](../userland/http-fetch.md#redirect-chains), 2026-10-09: bounded
delegated-provider chains with the final URL adopted by Links;
[QEMU qualification](../development/experiments/http-redirects/README.md).

[HTTP resources and transactions](../interfaces/http-transactions.md), 2026-10-16:
capability milestone B delivers complete snapshots, reusable configured endpoints,
generic stdio/captures and standalone scripts, retiring the interim HTTP surface.
[Usage and limits](../userland/http-fetch.md) and
[native qualification](../development/experiments/capability-http-retirement.md#owner-native-ab-2026-10-16).
General capability pipes remain in [milestone C](capability-model-implementation.md#milestone-c-capability-pipes).

[Kernel random generator](../devices/random-generator.md), 2026-10-08: BSP-owned
ChaCha20 with hardware reseeding; see the
[trust and availability limits](https://git.internal/PyxisOS/pyxis-os/issues/1469).

[Libc port sweep](../development/experiments/libc-port-sweep/README.md),
2026-10-10 (#683).

[LLVM toolchain on the host](../development/llvm-toolchain.md), 2026-10-07.

[Lua language server](../userland/lua-language-server.md), 2026-10-25: native
stdio workers, measured bounded queues, package/editor integration, typed API
completion, semantic tokens and diagnostics. Chronium qualified the editor on
vela HWP PXE; normal editor exit cleaned the server state. See the
[qualification and limits](../development/experiments/lua-language-server-qualification.md).

[Mounting Pyxis volumes on Linux](../development/npfs-linux-mount.md), 2026-10-06.

[Multiple network interfaces](../devices/network-interfaces.md), 2026-10-19:
retained interfaces, scoped authority, independent DHCP and explicit route/DNS/
management selections (#969, #973, #976, #987). Both wired ThinkPad roles pass
[native qualification](../development/experiments/network-interfaces-native.md)
with DNS supplements and an owner-present unplug observation; deeper lifecycle
checks remain [technical debt](https://git.internal/PyxisOS/pyxis-os/issues/1279).

[Name limits and links](../userland/libc-portability.md#name-limits-and-symbolic-links),
2026-10-10 (#687): `NAME_MAX`, O_NOFOLLOW, ELOOP and ENAMETOOLONG from new kernel
statuses.

[Native command execution](../userland/native-jobs.md), 2026-10-12: direct jobs,
private capture, concurrent pipelines, declared Lua contexts and exact C/libuv
adapters (#745, #749, #754, #757, #760). Shell syntax is unchanged; surviving
cleanup supervision and descendants remain with Continuum/nested groups.
[Qualification](../userland/native-jobs.md#adapter-qualification-2026-10-12).

[Native npfs hard links](../interfaces/native-hard-links.md), 2026-10-19:
regular-file aliases, counted lifetime, lazy feature activation, fsck/replay,
SDK/libc `link` and `ln`, with explicit sync/reboot qualification on a disposable
ThinkPad USB pool. [Measurements and limits](../development/experiments/npfs-hard-links-task4/README.md)
include the metadata ABI rebuild requirement for independent toolchain bundles.

[Native threads](../interfaces/threads.md), 2026-10-13: native siblings, address
parking, static TLS, the thread-safe libc and C11/pthread profile, and the SDL
workers, timers and audio callbacks (gates 1–9). The libuv pool, Hax and Go
adapters need separate assignments.

[Neovim](../userland/neovim.md), 2026-10-14: tasks 1–6 delivered and used natively on
the ThinkPad; [follow-ups](later-os-directions.md#neovim-follow-ups) are separate.

[Network kernel debugger](../kernel/network-debugger.md), 2026-10-09: tasks 1–3, read-only
LAN attach on the RTL8111, natively qualified (#652). Task 4 (mutation and execution
control) is unassigned.

[Network throughput](../development/network-throughput.md), 2026-10-09: on-link
segments, larger windows and Nagle off, natively measured; see the
[remaining limits](https://git.internal/PyxisOS/pyxis-os/issues/1437).

[npfs, installer and system updates](../devices/filesystem-readonly.md), qualified
natively on the ThinkPad: the [kernel writer](../devices/filesystem-native-adapter.md),
the [native installer](../userland/installer.md), [system updates](../userland/system-updates.md)
and [USB installation](../devices/usb-installation.md).

[Package and release upgrades](../userland/package-upgrades.md), 2026-10-26:
whole-set package updates, one active release, verified reboot-time set selection,
owned Classic/Coexist publication and reviewed signed-index floors. QEMU/OVMF
qualified; the [release-day vela gate](https://git.internal/PyxisOS/pyxis-os/issues/1509)
remains open.

[PCI BAR assignment](../devices/pci.md#assigning-an-unassigned-memory-bar),
2026-10-15 (#897): claimed-device memory BARs placed from ACPI root apertures with
a conservative collision proof, native-qualified on vela.

[Pointer input quality](../devices/pointer-input-quality.md), 2026-10-13:
report-protocol USB mice/shared scroll (#716), calibrated Synaptics EW/AGM palms,
typing, paired scroll/physical drag (#739/#773), and calibrated-profile-only
default activation. Owner T14 qualification passes; other profiles, USB axes
and terminal application reporting remain in technical debt.

[Presentation timing and pacing](../kernel/display.md#presentation-timing), 2026-10-09: RAM staging
and the completed-frame handoff (#610, #618), the Renoir timing observer and the
page-flip backend; [timing for programs](later-os-directions.md#presentation-timing-for-programs)
is unassigned.

[Print Screen key](../userland/screenshot.md#print-screen), 2026-10-18 (#954): a
system binding saves the shown screen to `home://screenshots/`; the
[native check](https://git.internal/PyxisOS/pyxis-os/issues/1353) is open.

[Program image and initial stack capacity](../kernel/program-loading.md),
2026-10-09: 256 MiB image span and 1 MiB guarded stacks, with
[matched costs](../development/experiments/program-capacity/README.md). Native
sibling lifecycle, address parking, static language TLS and the selected
thread-safe libc profile are delivered; consumer adapters remain in
[threads](../interfaces/threads.md).

[Remote debugging without serial](../development/remote-debugging.md), 2026-10-07; [panic and fault backtraces](../development/remote-debugging.md#panic-and-fault-backtraces)
followed on 2026-10-19 (#1010).

[File transfer through the remote terminal](../userland/remote-terminal.md#explicit-file-transfer),
2026-10-14: explicit `xfer` transfer, streaming and drag-and-drop upload, with the
owner's GUI-drop check passed.

[Remote multiplexer](../userland/remote-terminal.md#remote-multiplexer), 2026-10-11:
brokered panes, [splits/cwd/focus](../userland/multiplexer.md) and live host resize
(#728, #735), enabled in packaged live Remote (#736). The owner qualified pane
Neovim edit/save/quit, TCC hello world and instant live resize on main `a59a4ef3`;
[qualification and limits](../development/experiments/remote-multiplexer-resize.md).

[Renoir flip presentation](../kernel/renoir-flip.md), 2026-10-10: two-surface
GOP page flips (#631, #632, #638, #647), natively qualified; default-on where
qualified by owner decision 2026-10-14, with `display.flip=0` for rollback. See
the [remaining qualification](https://git.internal/PyxisOS/pyxis-os/issues/1339)
and [poll-cost qualification](../development/experiments/renoir-poll-cost/README.md).

[Runtime SMP](../kernel/smp.md), 2026-10-06, with
[topology-aware placement](../kernel/smp.md#placement-and-migration).

[Saved shell history](../userland/shell.md), 2026-10-10 (#609).

[Screenshots](../userland/screenshot.md), 2026-10-08: native CAPTURE and staged
PNG output, [checked natively](../development/screenshot-qualification.md#native-thinkpad-check).

[SDL game ports](../development/sdl-game-ports.md), 2026-10-09: Chocolate Doom,
Chocolate Quake and the opt-in EDuke32 build, without sound (#623, #634, #646);
see the [remaining native checks](https://git.internal/PyxisOS/pyxis-os/issues/1401).

[SDL2 with a native backend](../development/sdl2.md), 2026-10-08: upstream SDL 2.32.10
and [DevilutionX](../userland/devilutionx.md) as the first consumer;
[checked natively](https://git.internal/PyxisOS/pyxis-os/issues/1399).

[Shell conditions, merged output and script runtime](../userland/shell.md),
2026-10-18 (#949, #957): `&&`/`||`, `exit N`, ordered `2>&1` and runtime-only
[script re-entry](../userland/script-launch.md) for Neovim `:make`, with the
[qualification record](../development/experiments/shell-gaps.md).

[Shell reverse history search](../userland/shell.md), 2026-10-10 (#696): Ctrl+R in
the shared line editor.

[System layout](../userland/system-layout.md), 2026-10-07: `boot://`, `bin://`,
boot init with a Lua configuration, and a persistent home.

[System pointer](../interfaces/pointer.md), 2026-10-09: tasks 1–5 merged (#545,
#550, #560, #562); see the [qualification report](../development/system-pointer-qualification.md)
and the [open native checks](https://git.internal/PyxisOS/pyxis-os/issues/1342).
Clipboard and other input sources are separate milestones.

[System power overlay](../kernel/power-overlay.md), 2026-10-10 (#659, #710): the
installed-system flush check is pending, with parked follow-ups in
[system-power-overlay.md](later-os-directions.md#power-and-acpi).

[Terminal multiplexer](../userland/multiplexer.md), 2026-10-08: up to eight panes
with colored scrollback.

[ThinkPad bring-up](../targets/t14-gen1-amd/notes.md#native-status).

[USB HID boot keyboards and mice](../devices/usb-hid.md), 2026-10-10 (#654): QEMU
and ThinkPad input qualified, with [native coverage still open](https://git.internal/PyxisOS/pyxis-os/issues/1291).

[USB storage read throughput](../devices/usb-storage.md#bulk-read-aggregation),
2026-10-13 (#804): bounded npfs read aggregation took the native Clang compile from
about 4.3 s to 1.1 s. The shared-worker responsiveness check is still
[open](https://git.internal/PyxisOS/pyxis-os/issues/1385).

[Userland fonts](../userland/fonts.md), 2026-10-21: `libfont` (stb_truetype), the
`fonts` package with nineteen fonts and the `font-min` example (#959, #963, #964,
ports #138); built with hosted Clang and `-lfont` and run on vela.

[Volume controls](../userland/audio-volume.md), 2026-10-09: master and per-space
gain; see the [remaining native regression](https://git.internal/PyxisOS/pyxis-os/issues/1389).

[xfer pipelining](../development/experiments/xfer-pipelining/README.md),
2026-10-10 (#600): native runs follow its measurements and native steps.

[XSAVE/AVX](../kernel/xsave-avx.md), 2026-10-26: qualified boot-wide x87/SSE/AVX,
eager owned task state, legacy fallback and full sibling inheritance; QEMU and
chronium-run vela/ThinkPad checks passed. Extended debugger registers and
AVX-512 remain deferred.
