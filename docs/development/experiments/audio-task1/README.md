# Audio task 1: contracts and controller/codec bring-up

Raw captures, transcripts, patches, scripts and screenshots once kept in this directory were removed from the tree; Git history keeps them at `d6733033`.

Owner assignment **2026-10-08**. [#549](https://git.internal/PyxisOS/pyxis-os/pulls/549) merged into
main `ad0db38`; task branch `audio/controller-codec` was rebased onto it (the earlier baseline from
`abbeded` has the same kernel, ABI and pinned consumer inputs; #549 added documentation only).
Controller/codec bring-up with BSP-owned DMA was assigned, QEMU first with native checks in the later
batch; public grants/sessions/mixing, IRQ refill and consumers remain later tasks.

## Accepted task-specific decisions

Accepted by the owner through the orchestrator on **2026-10-08**, before code:

1. Document ACQUIRE/WRITE/STATUS/RELEASE using native message headers; WRITE carries a caller
   address/byte count and STATUS reports queue capacity and discontinuities. Implement only the private
   engine now, public calls and sessions in their later task.
2. Copied nonblocking all-or-nothing WRITE, at most 4096 bytes aligned to four-byte stereo frames; a
   full queue returns CALL_WOULD_BLOCK with no acceptance. WAIT_WRITABLE guarantees space for a maximum
   write. No drain promise or retained caller buffer.
3. Existing errors: CALL_UNAVAILABLE for absent/unsupported/failed hardware, CALL_BUSY for an acquired
   session, CALL_LIMIT for the ninth active session, CALL_NO_MEMORY on allocation, CALL_DENIED for
   missing authority or wrong owner.

The [accepted contract](../../../wip/hda-playback.md#accepted-session-call-contract) documents the
layouts and behavior without exporting placeholder ABI.

## No-audio baseline

Captured before implementation from clean main `abbededa6a` (includes pointer #545). Ordinary
`make -j16 image` passed in the LLVM 23.1.3/`49e2c1a` builder with the pinned sources rebuilt (no stale
pre-pointer bundles); vendored sbase/Quake warnings remain, so no warning-free whole-build claim is made.
Stock QEMU 10.2.2, Q35, `-cpu max`, four cores/one thread each, 8 GiB, nested KVM, fresh OVMF
variables, default VGA 1280x800, Caelum selected with a static TTY cursor, all three user spaces and
network ready and no remote client during the idle cost window. VirtIO SCSI read-only CD-ROM, NIC and RNG;
`intel-hda` plus `hda-output` with a 48 kHz S16 stereo WAV backend were present but no HDA driver or
consumer. The command shape matches the
[investigation](../audio-investigation/README.md#guest-configuration-and-commands) plus a QEMU pidfile for
host accounting; no guest instrumentation or automation.

**Presenter elapsed.** Eight manual hardware-breakpoint/`finish` pairs around `space_present` with the
matching ELF and `set may-call-functions off`, HPET counter at `0xfffffe80402020f0` (period 10 ns), per
the [screenshot timing method](../../screenshot-qualification.md#matched-presenter-cost); no inferior calls
or state edits, and GDB confirmed `active_space="caelum"`, 1280x800 and the BSP presenter task. Median
**0.854250 ms**, range **0.689460–1.328460 ms** (including preemption, device waits and nested-host
variation; not isolated CPU time or native performance).

**Host cost with idle guest.** No debugger during this separate window; two manual `/proc/<pid>/stat` and
thread-stat snapshots with host monotonic timestamps and `CLK_TCK=100`. Elapsed **74.321553919 s**, QEMU
process CPU **14.84 s**, or **19.9673% of one host CPU** (nested KVM execution/exits and QEMU overhead with
presenter, timers and network idle; not guest CPU utilization). Per-thread reads are sequential and
jiffy-rounded:

| Guest vCPU thread | Host CPU seconds | Percent of one host CPU |
| --- | ---: | ---: |
| CPU0 / BSP | 7.51 | 10.1047% |
| CPU1 | 3.16 | 4.2518% |
| CPU2 | 2.22 | 2.9870% |
| CPU3 | 1.96 | 2.6372% |

A single window is not a stable native utilization or regression threshold. Repeats after bring-up use
the same configuration, idle selection and initrd, report the actual window and normalize CPU time, and
record engine-idle and active-playback cost separately. No native host was accessed.

## Implemented engine and qualification

The [private engine](../../../devices/hda.md) initializes QEMU only, selects a checked analog route
without activation, stops command DMA/BME and parks its sole BSP worker indefinitely. No ABI header,
grant, session/mixer, IRQ/refill or application backend is added. Driver allocations are CORB/RIRB/BDL
4 KiB each and PCM 8 KiB backing; the static cyclic stream uses 7,680 bytes in four 1,920-byte periods.
Later producer/refill work must not replay stale PCM silently.

Production measured head **`9e456bd`**, with the baseline initrd unchanged (only the kernel differs;
compiler, firmware, hardware layout and consumer inputs matched) and source image builds passing without
kernel warnings. The production serial and debugger records show 22 commands/replies, codec `1af4:0012`,
pin 3 → DAC 2, a live untimed parked worker, command/stream DMA off, PCI command `0x402` (BME clear,
INTx disabled) and retained backing; the normal WAV has zero frames. A separate one-CPU boot without HDA
stays usable and selects no controller or worker. An actual first boot exposed premature STATESTS
acknowledgement erasing QEMU's reset-latched detection (normal shutdown with retained backing); the final
code reads detection before W1C, following QEMU's reset semantics and the specification's reset ordering.
This is a real bring-up correction, not injected failure qualification; other failure paths are
source-reviewed.

**Matched idle observations.** Same static scene, four CPUs and device order; eight presenter samples
(75363, 107523, 90055, 68410, 116706, 120965, 77017, 115768 HPET ticks).

| Observation | Before | Production engine idle |
| --- | ---: | ---: |
| Presenter median | 0.854250 ms | 0.987890 ms |
| Presenter range | 0.689460–1.328460 ms | 0.684100–1.209650 ms |
| Host idle window | 74.321554 s | 107.236802 s |
| QEMU process host CPU time | 14.84 s | 18.49 s |
| QEMU host CPU / wall time | 19.9673% of one CPU | 17.2422% of one CPU |

The presenter median rose 15.6% with overlapping ranges, and normalized host CPU fell 13.6%; windows of
different length are recorded, not compared as raw seconds. Per-thread host CPU seconds were
8.76/2.87/4.05/2.79 (8.1688/2.6763/3.7767/2.6017% of one CPU). Small samples and single windows establish
no stable regression or improvement, and these are nested-KVM elapsed and host-cost observations, not
native or guest-idle utilization. Source inspection and GDB establish no recurring engine wake or poll
while idle. Active-playback CPU cost was not measured; IRQ/refill/mixer work must measure it later.

**Unmerged consumer and known PCM.** Branch `probe/audio-task1-qualification` (head `ced1443`) adds only a
private consumer and worker invocation over `9e456bd`, kept unmerged (not a boot tone or self-test); the
complete initrd is still the baseline's. It submits 310 vendor reads across the 256-entry rings, then
activates, copies and runs the static 40 ms buffer for about one second, stops physical DMA,
detaches/mutes the converter, reopens 100 ms of zeros and shuts down stream/rings/link/BME, polling
position every 5 ms. It never overwrites running DMA or exercises software refill; commands during
playback are stopped while BME stays enabled for the stream and re-enabled only for later codec commands.

| QEMU run | Vendor-read ring pointers | Final commands/replies | Tone elapsed including stop/detach | Polled BCIS / observed wraps | Captured tone / zero frames |
| --- | --- | ---: | ---: | --- | --- |
| Four CPUs, `hda-output` | CORB/RIRB 54/54 after 310 reads | 366/366 | 1009.881 ms | 101 / 25 | 48,761 / 4,278 |
| One CPU, `hda-duplex` | CORB/RIRB 54/54 after 310 reads | 376/376 | 1032.612 ms | 100 / 25 | 48,465 / 4,786 |

Both runs ended with no engine failure, stopped/reset stream, rings and link, BME off, retained DMA and no
unsolicited replies. LPIB is modulo the cyclic buffer and polled BCIS and wrap counts can miss or
coalesce events, so they prove neither precise hardware-period accounting, audible drain nor sustained
dynamic refill. Duplex's unavailable ADC-backend warning is expected for output-only WAV. The inventory
and five echo commands completed afterwards (responsiveness, not concurrent audio load).

Every captured nonzero frame matched the expected independent channels exactly: left a 1 kHz triangle,
peak ±8192, period 48 samples; right a 500 Hz triangle, peak ±4096, period 96. For amplitude A, period P
and phase p=i%P: `x=-A+floor(p*4*A/P)` for p<P/2, otherwise `x=3*A-floor(p*4*A/P)`. Both periods divide
the 1,920-frame cyclic buffer and every frame after the tone is zero. Comparison used standard Python
wave/struct only. The output WAV had 53,039 frames (212,200 bytes) and the duplex WAV 53,251 frames
(213,048 bytes), both stereo S16 at 48 kHz.

## Delivery and remaining work

Task 1 settles the documented session contract and delivers the private controller/codec stage,
corresponding to the milestone's contract and engine checkboxes. The shipped worker initializes quietly
and parks. IRQ/refill, per-space sessions and software mixing, native binding and qualification, and
consumers remain unassigned later tasks; the ALC257 data is kept for the native route and QEMU evidence
does not qualify it. Confirm the owner's QEMU-closure/native-batch choice at milestone closure.

The review of [#553](https://git.internal/PyxisOS/pyxis-os/pulls/553) requested one success summary and
quiet absence: controller/codec success details, normal shutdown and the QEMU pin-control detail now use
ktrace, while ambiguous or incomplete PCI selection and failure explanations (including failed shutdown
readbacks) stay klog. This changes logging severity only; the measurements above kept the original log
output and the qualifier stays at its recorded revision.
