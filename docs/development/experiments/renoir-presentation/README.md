# Renoir observation and blank-start copy qualification

Presentation step 2, accepted 2026-10-09 after #622. The source implementation
is at Pyxis `20bf9f8f`, based on main `31af2224`; the
[display reference](../../../kernel/display.md#read-only-renoir-firmware-timing)
describes its contracts. Native Pyxis qualification is **pending**. Timed copies
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

## Native ThinkPad batch

Use the reviewed implementation revision, wired AC power, one internal panel
and the unchanged native GOP mode. Record revision, ELF/image hashes, BIOS/panel
configuration, clock source and the source/dependency/compiler identities above.
Do not set `DISPLAY_SIZE`, attach another display, change power/mode/clock
settings, or run a debugger during latency samples. Firmware timing may differ
from Fedora's 1920x1080 / 60.0204 Hz / 31-line reference; qualify the actual boot.

Build three images from the **same source revision and private Quake data**, in
the usual compiler environment. Set `QUAKE_DATA` to the local id1 directory:

```sh
mkdir -p build/renoir-native
make -j16 image DISPLAY_TIMING=off QUAKE_DATA=/path/to/id1
cp build/pyxis.iso build/renoir-native/off.iso
cp build/caelum.elf build/renoir-native/off.elf
cp build/initrd.cpio build/renoir-native/off.cpio
cp build/limine.conf build/renoir-native/off.conf
make -j16 image DISPLAY_TIMING=observe QUAKE_DATA=/path/to/id1
cp build/pyxis.iso build/renoir-native/observe.iso
cp build/caelum.elf build/renoir-native/observe.elf
cp build/initrd.cpio build/renoir-native/observe.cpio
cp build/limine.conf build/renoir-native/observe.conf
make -j16 image DISPLAY_TIMING=blank QUAKE_DATA=/path/to/id1
cp build/pyxis.iso build/renoir-native/blank.iso
cp build/caelum.elf build/renoir-native/blank.elf
cp build/initrd.cpio build/renoir-native/blank.cpio
cp build/limine.conf build/renoir-native/blank.conf
sha256sum build/renoir-native/*
```

Use the owner's existing PXE staging procedure with the
[generated boot configuration](../../configuration.md#boot-menu-timeout)
to select each variant, retaining its kernel, initrd and generated
`build/limine.conf` together. `DISPLAY_TIMING` writes the boot option into normal,
rescue and installer entries; changing a runtime shell environment cannot change
the kernel policy. An omitted value defaults to `observe`; invalid generated
values are refused, and an invalid manually supplied kernel value falls back to
observation. Duplicate kernel options retain the parser's existing fatal rule.

1. **Off baseline:** boot `off`, idle at the prompt for 40 seconds, then run
   ordinary `quake` for 40 seconds. Take a short fixed-angle camera clip while
   turning steadily in the same scene; note visible tear lines and input feel.
   This is the existing staged unsynchronized copy, with no observer mapping.
2. **Observer/counter capture:** boot `observe`. Save `log` output after startup
   and after the same idle and Quake intervals. Collect `renoir-otg` identity,
   raw H/V timing/control fields, decoded active/total/blank, frame and vertical
   positions, STATUS, advancing transition count and bracket width. Save the
   `display-timing: hardware` period/line/blank/uncertainty line, or the complete
   unavailable/unqualified reason. Check four observed transitions, both blank
   states, advancing idle and animation counters, no stalls or mode changes.
   Static blank bits on disabled OTGs are not timing. No PSR wake/disable occurs.
3. **Unsynchronized start distribution:** from the `observe` logs retain three
   complete 600-copy summaries after warmup for idle and moving Quake. They give
   start upper-bound, copy/fence, read bracket, spin and wake P50/P95/P99/max,
   admitted/unsynchronized/invalid counts, missed-start late/poll-expired/
   unqualified reasons, frame/position/status and prefix gaps.
   Observe always counts copies unsynchronized. A start outside blank is useful
   baseline phase data, not an admission. Unqualified samples are excluded from
   start quantiles, with their count explicit.
4. **Qualification opt-in:** only then boot the labelled experimental `blank`
   image. Repeat the same idle/Quake intervals and collect three complete
   summaries. Admission needs remaining blank after an eight-line guard and
   timestamp/period uncertainty; late/expired/unqualified starts copy immediately
   once. Check the start P99/max and fallback counts against the measured window,
   not the nominal 16.67 ms refresh. Hardware timing can remain valid during an
   unsynchronized fallback. Conservative sparse requalification may leave phase
   unavailable even while counters advance.
5. **Visible result and correctness:** take the same camera clip during steady
   Quake turning, preferably with a high-frame-rate camera, and compare with
   `off`. Keep timedemo separate (it remains uncapped). Check mouse-look,
   Super+Esc/relock, space/layer changes, TTY selection and screenshots with a
   visible cursor; report input responsiveness alongside the distributions.
   A screenshot/FPS is not a tearing check. Stop each workload before the next.

The start marker timestamps one actual four-byte front store with IRQs saved,
then restores IF before remaining copying/accounting. GPU observation and waits
are never performed in that section; its clock can read HPET and has measured
access cost. NMI/device-fetch timing is not inferred from this CPU timestamp.
Copy completion timing includes MFENCE/LFENCE after the ordinary store fence.
Once per 60 copies, representative chunk/row-prefix timestamps report CPU-issued
progress gaps and guard overruns. They add clock cost on sampled frames and do
not prove WC visibility for every row. Every 600 copies, bounded summaries sort
after direct-writer release; keep outliers, reporting samples and invalid counts.

Accept native timed-copy qualification only with positive measured start margin,
no unexplained counter/mode loss or prefix guard overruns under the workload,
and camera/correctness evidence. The eight-line guard is a qualification policy,
not a measured AMD fetch depth. If these fail or evidence is missing, retain
default `observe`, record the failed condition and revisit before enablement.
There is no automatic opt-in, register-write fallback, page flip or program API.
