# Completed milestones

Completed milestones keep no WIP document: their behavior lives in the linked
references, and open native checks live in [technical debt](../technical-debt.md).
Entries are one short paragraph each, in alphabetical order. Add an entry only
when a milestone completes, as the [milestone index](boot-sdk-ports.md) says.

[ACPI](../kernel/acpi.md), 2026-10-07: power-off, restart, the power button and
battery.

[AX200 Bluetooth investigation](../devices/ax200-bluetooth.md), 2026-10-08:
warm-host QEMU passthrough through identification of the MX Master 3S; see the
[final report](../development/bluetooth-investigation.md). Production mouse work
remains in the [mouse milestone](bluetooth-mouse.md).

[C++ in userspace](../development/cxx-userspace.md), 2026-10-08: libc++, libc++abi
and libunwind in the SDK, with fmt as the first C++ port.

[Cheaper timekeeping](../kernel/timekeeping.md#clock-reads-in-timer-handling),
2026-10-10: fewer clock reads per timer event (#571) and the TSC with HPET
fallback (#577), natively qualified; [measurements](../development/experiments/tsc-clock/README.md).

[Deadline sleep wakeups](../kernel/timekeeping.md), 2026-10-08: per-CPU one-shot
LAPIC deadlines, [checked natively](../technical-debt.md#sleep-wake-granularity).

[Display drivers and resizing](../kernel/display.md), 2026-10-07.

[Everyday commands](../userland/everyday-commands.md), 2026-10-07.

[Graphics and terminal layers](../userland/space-layers.md), 2026-10-08:
Super+Up/Down, [checked natively](../technical-debt.md#space-layer-qualification).

[HD Audio investigation](../development/audio-investigation.md), 2026-10-08:
QEMU probes that identified the ALC257 and led to the playback milestone below.

[HD Audio playback](../devices/hda.md), 2026-10-09: BSP-owned analog engine and
[up to eight PCM sessions](../interfaces/audio.md), one per space. Open debt:
[live jack switching](../technical-debt.md#hd-audio-jack-routing-at-playback-start)
and the [nested-QEMU limit](../technical-debt.md#hd-audio-sustained-eight-session-playback).

[HTTP redirects](../userland/http-fetch.md#redirect-chains), 2026-10-09: bounded
delegated-provider chains with the final URL adopted by Links;
[QEMU qualification](../development/experiments/http-redirects/README.md).

[Kernel random generator](../devices/random-generator.md), 2026-10-08: BSP-owned
ChaCha20 with hardware reseeding; see the
[trust and availability limits](../technical-debt.md#random-generator-trust-and-availability).

[Libc port sweep](../development/experiments/libc-port-sweep/README.md),
2026-10-10 (#683).

[LLVM toolchain on the host](../development/llvm-toolchain.md), 2026-10-07.

[Mounting Pyxis volumes on Linux](../development/npfs-linux-mount.md), 2026-10-06.

[Network throughput](../development/network-throughput.md), 2026-10-09: on-link
segments, larger windows and Nagle off, natively measured; see the
[remaining limits](../technical-debt.md#tcp-throughput-limits).

[npfs, installer and system updates](../devices/filesystem-readonly.md), qualified
natively on the ThinkPad: the [kernel writer](../devices/filesystem-native-adapter.md),
the [native installer](../userland/installer.md), [system updates](../userland/system-updates.md)
and [USB installation](../devices/usb-installation.md).

[Presentation timing and pacing](presentation-timing.md), 2026-10-09: RAM staging
and the completed-frame handoff (#610, #618); step 3 is unassigned.

[Program image and initial stack capacity](../kernel/program-loading.md),
2026-10-09: 256 MiB image span and 1 MiB guarded stacks, with
[matched costs](../development/experiments/program-capacity/README.md). Thread
task 1 merged in #612; later thread tasks are unassigned.

[Remote debugging without serial](../development/remote-debugging.md), 2026-10-07.

[Renoir flip presentation](../kernel/renoir-flip.md), 2026-10-10: opt-in two-surface
GOP page flips (#631, #632, #638, #647), natively qualified with default off; see
the [remaining qualification](../technical-debt.md#renoir-flip-backend-qualification)
and [poll-cost qualification](../development/experiments/renoir-poll-cost/README.md).

[Runtime SMP](../kernel/smp.md), 2026-10-06, with
[topology-aware placement](../kernel/smp.md#placement-and-migration).

[Saved shell history](../userland/shell.md), 2026-10-10 (#609).

[Screenshots](../userland/screenshot.md), 2026-10-08: native CAPTURE and staged
PNG output, [checked natively](../development/screenshot-qualification.md#native-thinkpad-check).

[SDL game ports](../development/sdl-game-ports.md), 2026-10-09: Chocolate Doom,
Chocolate Quake and the opt-in EDuke32 build, without sound (#623, #634, #646);
see the [remaining native checks](../technical-debt.md#sdl-game-ports-native-qualification).

[SDL2 with a native backend](../development/sdl2.md), 2026-10-08: upstream SDL 2.32.10
and [DevilutionX](../userland/devilutionx.md) as the first consumer;
[checked natively](../technical-debt.md#sdl2-and-devilutionx-native-qualification).

[System layout](../userland/system-layout.md), 2026-10-07: `boot://`, `bin://`,
boot init with a Lua configuration, and a persistent home.

[System pointer](../interfaces/pointer.md), 2026-10-09: tasks 1–5 merged (#545,
#550, #560, #562); see the [qualification report](../development/system-pointer-qualification.md)
and the [open native checks](../technical-debt.md#native-system-pointer-qualification).
Clipboard and other input sources are separate milestones.

[System power overlay](system-power-overlay.md), 2026-10-10 (#659): the
installed-system flush check is pending.

[Terminal multiplexer](../userland/multiplexer.md), 2026-10-08: up to eight panes
with colored scrollback.

[ThinkPad bring-up](../targets/t14-gen1-amd/notes.md#native-status).

[USB HID boot keyboards and mice](../devices/usb-hid.md), 2026-10-10 (#654): QEMU
and ThinkPad input qualified, with [native coverage still open](../technical-debt.md#usb-hid-native-coverage).

[Volume controls](../userland/audio-volume.md), 2026-10-09: master and per-space
gain; see the [remaining native regression](../technical-debt.md#hd-audio-volume-native-regression).

[xfer pipelining](../development/experiments/xfer-pipelining/README.md),
2026-10-10 (#600): native runs follow its measurements and native steps.
