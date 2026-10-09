# Renoir observation and blank-start copy qualification

Presentation step 2, accepted 2026-10-09 after #622. The source implementation
is at Pyxis `20bf9f8f`, based on main `31af2224`; the
[display reference](../../../kernel/display.md#read-only-renoir-firmware-timing)
describes its current contracts. Native Pyxis qualification **failed** in batch 2;
the safety follow-up below awaits its native recheck. Timed copies
are off by default; neither the Linux reference nor QEMU establishes tear-free
native output.

## Builds and QEMU method

Before production code edits, baseline `31af2224` ordinary ISO/ELF, component
bundles, profiled kernel and initial profiled ISO were saved with hashes.
Cost sampling continued afterward from those unchanged inputs; the corrected
true-boot ISO was assembled later from the same archived profiled kernel.
Pinned userland `2fc6badb`,
ports `0f0aa443`, fs `b427df29` and lwIP `a1aadb91` are unchanged. Builder:
LLVM23.1.3/`49e2c1a`, digest
`50bfb587f2e73d6478ae1c91e86984ccb8223baa4367fe27fa227808e87bc038`.
The candidate ordinary default image passed with the existing builder; no
compiler-container rebuild or public ABI/dependency change.

The existing local `/shared/present/timing.py` patch is identical on both
sources and remains uncommitted. It samples composition and frame end through
the store fence, reporting three 600-frame summaries per workload after at
least ten seconds warmup. QEMU 10.2.2, Q35, nested KVM, `-cpu max`, four CPUs,
one socket/four cores/one thread, 8 GiB, fresh OVMF, 1280x800, relative PS/2,
VirtIO-SCSI CD/RNG, no NIC/disk/audio/USB, display off. VirtIO-SCSI CD avoids
the installed QEMU AHCI path; both sources use it. Nested QEMU rejected invariant
TSC and used HPET; timing includes its clock-read overhead.

True boot/std uses no `DISPLAY_SIZE` (GOP is already 1280x800); Bochs requests
1280x800. A first standard-VGA trial accidentally selected Bochs through the
size option and is excluded. GDB confirmed the actual boot/Bochs/VirtIO driver.
Workloads are the ordinary Development prompt/caret and moving Quake demos,
using the same locally supplied shareware data; data are not committed.
Background qualification VMs changed during the initial samples. They are
retained as contended distributions, not evidence of native cost or a speedup.
The matched-source/configuration samples below include all outliers. Repeated
attempts to obtain a quiet host were interrupted by other qualification VMs;
no isolated performance delta, no-regression result or native cost is claimed.
Local logs, images/hashes, background process records and captures remain under
`/shared/present/renoir-step2`. No timing patch or private game data is committed.

## QEMU results

Ranges cover three 600-frame summaries per workload, in milliseconds;
each entry is P50 / P95 / max. Same device/CPU/memory/clock/workload/image inputs,
with changing host load as described above.

| Composition | Baseline `31af2224` | Candidate `20bf9f8f` |
| --- | --- | --- |
| Boot, idle | .352–.354 / .696–.739 / .783–1.967 | .295–.304 / .370–.503 / .568–1.391 |
| Boot, Quake | .328–.529 / .897–1.572 / 1.992–11.230 | .277–.422 / .513–1.449 / 1.341–6.887 |
| Bochs, idle | .362–.367 / .576–1.226 / 1.048–2.435 | .296–.300 / .496–.508 / .586–1.829 |
| Bochs, Quake | .333–.349 / .646–.992 / 1.430–1.676 | .294–.297 / .515–.524 / .976–1.057 |
| VirtIO, idle | .525–.596 / .746–1.129 / 1.546–6.285 | .302–.305 / .508–.524 / .555–.625 |
| VirtIO, Quake | .320–.354 / .541–.780 / .923–2.985 | .270–.291 / .488–.524 / .949–1.376 |

| Frame end through fence | Baseline `31af2224` | Candidate `20bf9f8f` |
| --- | --- | --- |
| Boot, idle | .299–.301 / .560–.581 / 1.200–1.597 | .259–.264 / .311–.582 / .694–1.850 |
| Boot, Quake | .293–.517 / .679–1.800 / 1.787–13.514 | .245–.374 / .608–1.465 / 1.011–5.369 |
| Bochs, idle | .304–.305 / .483–.806 / 1.326–1.902 | .249–.254 / .427–.447 / .742–1.464 |
| Bochs, Quake | .297–.302 / .581–.620 / 1.371–2.163 | .268–.275 / .459–.467 / .669–1.458 |
| VirtIO, idle | 1.108–1.404 / 1.704–4.031 / 3.788–17.521 | .556–.566 / .882–.920 / 2.491–2.840 |
| VirtIO, Quake | .683–.816 / 1.060–1.365 / 2.561–3.111 | .526–.547 / .899–.991 / 2.557–2.700 |

Manual monitor input and read-only GDB (`set may-call-functions off`) checked:

- All three backends: ordinary prompt/key echo, pointer/caret, moving Quake,
  and truthful unavailable timing. Default mode was observe; the observer was
  unprepared and no hardware sequence/timestamp/period was fabricated.
- Existing `screenshot s.png`: native completed frame, visible I-beam included,
  and return without error on boot, Bochs and VirtIO; SHA-256 was obtained
  afterward for Bochs and VirtIO. Boot had no hash measurement.
  GDB raw-frame dumps were visually checked; guest functions/memory were not
  mutated. Monitor screendumps alone omit the VirtIO host cursor.
- Explicit blank boot: ordinary presentation/input continued; hardware
  capability stayed false/zero and no AMD device/window was mapped. Off boot
  also continued normally and did not even attempt observer preparation.
- Task-owned GTK/X11 VirtIO window: manual resize to 1000x700 then 800x600
  produced guest targets 1000x673 then 800x573 (GTK chrome excluded), TTY
  generation three, with observation still unavailable. Post-resize capture
  completed at 800x573 with the I-beam and successful PNG publication.

Measured PNG hashes for Bochs, VirtIO and post-resize respectively:

```text
bochs: c1208b425907004ce8fa0b7a73ddd5879b6aa0ceaf08c2cbee91fc0e07a4bf330
virtio: 3270010e0a0074ef766924d4c46cd95a7765f5c38ac17d0f20b220b5628647bb
resized: 47e89a0563cf790ad62f8820cedf19a649e3390e1329c6b93a1a980479373109
```

Independent source/existing-object review covered device/cache/mapping unwind,
counter rollover and unavailable/requalification guards, timestamp/store code,
bounded polling and private capability. Panic ordering was source-reviewed;
no forced panic, fault injection, allocation-refusal test or new test consumer
was added. Native observer reads, counter stall/rollover extremes, timing
admission and per-row WC visibility were not exercised in QEMU. All task-owned
QEMU, debugger and build jobs were stopped after qualification.
Missed-start reason counters were added afterward to native diagnostics only;
the unavailable QEMU path performs no such accounting or logging.
The review follow-up makes fine polling read only frame, position and status,
with full device/BAR/mode validation once per cadence and before an admitted
write. A subsequent review replaces global trace logging for register dumps
and periodic metrics with the explicit display-timing qualification flag at
info level: trace floods the ThinkPad console and was ruled out for timing
batches. These native-only changes do not provide new hardware cost or latency
measurements.

The qualification-flag follow-up built default and flagged images at info
level. A Q35/nested-KVM/four-CPU/8-GiB standard-VGA boot with
`display.timing=observe display.timing.metrics=1` reached normal startup.
Read-only GDB confirmed both parsed and presenter metrics flags, with the
observer unprepared and hardware capability zero. Serial retained one
unavailable summary and no register/periodic dumps. The native reporting path
is source-reviewed, not exercised by this QEMU check.

## Native batch 2 failure

Owner-reported ThinkPad T14 Gen 1 AMD results, 2026-10-09, PXE main `11d35fa6`,
`LOG_LEVEL=info`. Boots were default observe without metrics, off, observe with
metrics, then blank with metrics. Local evidence is
`/shared/present/batch2/caelum-batch2.log` and `results.md`; raw captures are not
committed here.

Measured estimates included 13.887, 16.663, 15.647, 20.785 and 16.650 ms versus
Linux's 16.661 ms. A 40.7 ms bound still qualified hardware. Default boot printed
sixteen observation losses and an unsupported `%02x` corrupted the device/BAR
summary. Observe's start-upper included 4–37 ms. Blank admitted 13 of about
6000 copies, yet the owner saw tearing apparently lasting 2–3 frames and a
couple of frames of input delay. Copy/fence P50 was about 1.17 ms.

Source inspection found that a hardware deadline overwrote the software cadence
accumulator, and post-composition requalification could sleep toward a second
blank. The safety fix removes all per-frame phasing/sleep/poll paths and uses
the same full observation in both modes, with opportunistic blank classification
only.
It fixes the format, traces repeated losses and rejects uncertainty exceeding
blank minus the existing eight-line guard before publishing hardware capability.
A live widened bound revokes qualification. Rejected counts/max width join the
existing metrics-gated summary, without adding default log lines. The unused
light-read helper and obsolete poll-expiry, spin and wake-lateness metrics are
removed; they no longer describe work performed by this path.

The estimator remains unchanged. EDID anchoring and estimator overhaul were
explicitly dropped by the owner. Narrow uncertainty does not establish correct
period or tear-free output. Start-upper is age since an estimated preceding
blank, not proof of a within-blank start; active-scanout copies can have
millisecond ages. No native safety-fix result is claimed yet.

## Native ThinkPad batch

Batch 2 failed qualification. The current owner-requested two-boot recheck is
below; the original three-mode qualification procedure is superseded.

### Native safety recheck

Two boots only, the same reviewed revision and private Quake data, wired AC,
unchanged GOP mode and peripherals. Use the default info log level; global
trace was ruled out because it floods the console and skews timing. Build and
save each kernel/initrd/generated boot configuration together using the existing
compiler environment:

```sh
mkdir -p build/renoir-safety
make -j16 image LOG_LEVEL=info DISPLAY_TIMING=observe DISPLAY_TIMING_METRICS=1 QUAKE_DATA=/path/to/id1
cp build/pyxis.iso build/renoir-safety/observe.iso
cp build/caelum.elf build/renoir-safety/observe.elf
cp build/initrd.cpio build/renoir-safety/observe.cpio
cp build/limine.conf build/renoir-safety/observe.conf
make -j16 image LOG_LEVEL=info DISPLAY_TIMING=blank DISPLAY_TIMING_METRICS=1 QUAKE_DATA=/path/to/id1
cp build/pyxis.iso build/renoir-safety/blank.iso
cp build/caelum.elf build/renoir-safety/blank.elf
cp build/initrd.cpio build/renoir-safety/blank.cpio
cp build/limine.conf build/renoir-safety/blank.conf
sha256sum build/renoir-safety/*
```

Use the owner's existing PXE staging procedure. Record revision, hashes, clock
source, mode and workload for both boots.

1. **Observe + metrics:** idle for 40 seconds, then ordinary moving `quake` for
   40 seconds. Save three complete 600-copy summaries after warmup and a short
   camera clip of steady turning. Check the PCI/BAR preparation line renders
   correctly and repeated losses do not appear in the info log. `source=hardware`
   must have uncertainty below `(blank_lines - 8) * period / V_total` (about
   345 µs for this panel); record `rejected-bounds` and `reject-max`. Tens-of-ms
   bounds must remain unqualified. Keep outliers and invalid sample counts.
2. **Blank + metrics:** repeat the same intervals, scene and camera angle.
   Software cadence, full observation and copying are identical to observe;
   no blank phasing/sleep/poll executes. Opportunistic admissions may be rare
   and never delay a fallback copy. Compare tear persistence and input feel,
   including mouse-look, Super+Esc/relock, typing and space/layer changes.
   Return to observe if unexplained additional tearing or delay remains.

This recheck qualifies the safety/fallback correction only. It does not accept
period accuracy, blank-copy tear-free output, or a default timing-policy change.
Screenshots/FPS are not tearing checks. Existing prepared RO/NX/UC mappings,
panic ownership, actual first-store marker, copy fences and capture contracts
remain. No EDID, modeset, clocks, power, firmware or GPU register writes are added.

## Safety-fix QEMU checks

Pre-code baseline `b0a050b7` ISO/ELF/initrd/configuration was archived. Code
`c51122ba` built default, observe+metrics and blank+metrics images using the
existing LLVM23.1.3/49e2c1a builder and verified unchanged SDK/userland/ports
bundles. Observe and blank kernel ELF and initrd were byte-identical; only
boot policy differed.

Fresh QEMU10.2.2/Q35/nested-KVM/four-core/2-GiB standard-VGA boots used fresh
OVMF variables, VirtIO-SCSI CD and RNG, relative PS/2, no network/disk/audio/USB,
and a disabled host window. Read-only GDB confirmed DISPLAY_BOOT, the respective
mode, an unprepared observer and zero hardware capability/period/timestamp.
Both retained one unavailable summary, tab switching, keyboard input and `ls`
completion; the pointer/caret and screen were visually checked. PNGs after
the same command were identical (SHA-256
`830a8fd53863ea0a5de544b8b6f09009c1e0ff1a9f48b56e723835a2ce9f3295`).

Cadence parity, qualification publication, guarded rejection and supported
formatting were independently source-reviewed. QEMU cannot exercise native
Renoir registers, period estimation, rejected hardware bounds or visible tearing;
no native or performance pass is claimed. No new tests, device emulation, fault
injection or boot/input automation were added. Task-owned QEMU/GDB/build jobs
stopped; artifacts and captures remain local and ignored.
