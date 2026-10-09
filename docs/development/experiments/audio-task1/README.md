# Audio task 1: contracts and controller/codec bring-up

Raw captures, transcripts, patches, scripts and screenshots once kept in this directory were removed from the tree; Git history keeps them at `d6733033`.

In progress, owner assignment **2026-10-08**. [#549](https://git.internal/PyxisOS/pyxis-os/pulls/549)
merged into fresh main `ad0db38`; task branch `audio/controller-codec` is rebased
onto it. The earlier baseline from `abbeded` has the same kernel, ABI and pinned
consumer inputs; #549 added documentation only. Controller/codec bring-up with
BSP-owned DMA is assigned, QEMU first, with native checks in the later batch.
Public grants/sessions/mixing, IRQ refill and consumers remain later tasks.

## Accepted task-specific decisions

Accepted by the owner through the orchestrator on **2026-10-08**, before code:

1. Document ACQUIRE/WRITE/STATUS/RELEASE using native message headers; WRITE
   carries a caller address/byte count and STATUS reports queue capacity and
   discontinuities. Implement only the private engine now, public calls/sessions
   in their later task.
2. Copied nonblocking all-or-nothing WRITE, at most 4096 bytes aligned to four-byte
   stereo frames; full queue returns CALL_WOULD_BLOCK with no acceptance.
   WAIT_WRITABLE guarantees space for a maximum write. No drain promise or
   retained caller buffer.
3. Existing errors: CALL_UNAVAILABLE for absent/unsupported/failed hardware,
   CALL_BUSY for an acquired session, CALL_LIMIT for the ninth active session,
   CALL_NO_MEMORY on allocation, CALL_DENIED for missing authority/wrong owner.

The [accepted contract](../../../wip/hda-playback.md#accepted-session-call-contract)
documents the intended layouts and behavior without exporting placeholder ABI.
No new owner decision is needed to begin the assigned private engine.

## No-audio baseline

Captured before implementation, from clean main `abbededa6a` (includes merged
pointer #545). Ordinary `make -j16 image` passed in the existing LLVM
23.1.3/49e2c1a builder. Pinned sources were rebuilt, including the new pointer
userland/ports revisions; no stale pre-pointer bundles were used. Vendored
sbase/Quake sources emit existing warnings; no warning-free whole-build claim.
Provenance (kernel, SDK, userland, ports and configuration) was recorded at build time.

| Artifact | SHA-256 |
| --- | --- |
| ELF | `9b08b75e01d87de30e560e0aeac7de27c2982cf1e80e1f419145752ecbbcfee7` |
| ISO | `fbd04c4259770460a7a20ad76ab52968eefe2b03563607365d961aa81e11eb07` |
| Initrd | `3dbc113e9cd4df7111ffdfa1eb4a31cd5549f1abcedc1f26509133062a28830c` |

Stock QEMU 10.2.2, Q35, `-cpu max`, four cores/one thread each, 8 GiB, nested KVM,
fresh OVMF variables, default VGA 1280x800. Caelum selected, static TTY cursor,
all three user spaces/network ready; no remote client connected during the idle
cost window. Modern VirtIO SCSI read-only CD-ROM, NIC and RNG; `intel-hda` plus
`hda-output` and WAV backend (48 kHz S16 stereo) present but no HDA driver/audio
consumer. Command shape matches the [investigation](../audio-investigation/README.md#guest-configuration-and-commands),
with matching baseline ELF/ISO and local filenames. Added only a QEMU pidfile
for host accounting; no guest instrumentation or new boot/output automation.

### Presenter elapsed observations

Eight individually entered hardware-breakpoint/`finish` pairs around
`space_present`, using the matching ELF and `set may-call-functions off`.
HPET counter reads at `0xfffffe80402020f0`, period 10 ns, following the existing
[screenshot timing method](../../screenshot-qualification.md#matched-presenter-cost).
No inferior calls or guest state edits. GDB confirmed `active_space="caelum"`,
1280x800 and the BSP presenter task.

Ticks: **72269, 73700, 86171, 85435, 132846, 68946, 85415, 99932**.
Median **0.854250 ms**, range **0.689460–1.328460 ms**. These are debugger-qualified
elapsed observations including preemption/device waits and nested-host variation,
not isolated CPU time or native performance.

### Host cost with idle guest

No debugger attached during this separate window. Two manual `/proc/<pid>/stat`
and thread-stat snapshots, with host monotonic timestamps and `CLK_TCK=100`:
start and end. Fields are user
and system CPU ticks, not guest-idle percentages. QEMU monitor `info cpus`
identified CPU0/1/2/3 thread IDs 355643/355644/355645/355646.

Elapsed **74.321553919 s**; QEMU process CPU **14.84 s**, or **19.9673% of one
host CPU**. This includes nested KVM execution/exits and QEMU overhead with the
presenter/timers/network idle, not guest CPU utilization. Per-thread reads are
sequential and jiffy-rounded, so their sums can differ slightly from the process.

| Guest vCPU thread | Host CPU seconds | Percent of one host CPU |
| --- | ---: | ---: |
| CPU0 / BSP | 7.51 | 10.1047% |
| CPU1 | 3.16 | 4.2518% |
| CPU2 | 2.22 | 2.9870% |
| CPU3 | 1.96 | 2.6372% |

Repeat both methods after bring-up with the same configuration, idle selection
and initrd/bundle bytes; report actual window duration and normalize CPU time.
Record engine-idle and any active-playback cost separately. A single window is
not a stable native utilization or regression threshold. All baseline guest,
debugger and build processes stopped. Local baseline bundles were packaged for
matched reuse. No native host was accessed and the ALC257 dump is unchanged.

## Implemented engine and qualification

The [private engine](../../../devices/hda.md) initializes QEMU only, selects a
checked analog route without activation, stops command DMA/BME and parks its
sole BSP worker indefinitely. No ABI header/grant, session/mixer, IRQ/refill or
application backend is added. Driver allocations are CORB/RIRB/BDL 4 KiB each
and PCM 8 KiB backing; the static cyclic stream uses 7,680 bytes in four 1,920-byte
periods. Later producer/refill work must not replay stale PCM silently.

Production measured code head **`9e456bd`**:
ELF `c98c62175aa8d3bcfb2a1cf23aae2b3d8385a4536ff764878660236e122439da`,
ISO `5fdf63ce01b7fb67153d00c70d0308cf8f2c7418479de31e9ec47ca5eb414812`.
The initrd remains exactly the baseline
`3dbc113e9cd4df7111ffdfa1eb4a31cd5549f1abcedc1f26509133062a28830c`.
Only the kernel changed; compiler, firmware, hardware layout and consumer inputs
are matched. Source image builds passed without kernel warnings using the
verified baseline SDK/userland/ports bundles.

The production serial and debugger records
show 22 commands/replies, discovered codec `1af4:0012`, pin 3 → DAC 2, a live
untimed parked worker, command/stream DMA off, PCI command `0x402` (BME clear,
INTx disabled), and retained backing. The normal WAV has zero frames. A separate
one-CPU boot without HDA remains usable (serial and
inventory/echo checks); absence selects no controller/worker.
No native hardware was accessed.

An actual first boot exposed premature STATESTS acknowledgement erasing QEMU's
reset-latched detection. Its failure record confirmed
normal shutdown/retained backing. The final code reads detection before W1C,
following QEMU's reset semantics and specification reset ordering. This is a
real bring-up correction, not injected failure qualification; other failure
paths remain source-reviewed.

### Matched idle observations after bring-up

Same static Caelum 1280x800 scene, four CPUs and device order. Eight individually
entered presenter samples, same method as baseline:
**75363, 107523, 90055, 68410, 116706, 120965, 77017, 115768** HPET ticks.

| Observation | Before | Production engine idle |
| --- | ---: | ---: |
| Presenter median | 0.854250 ms | 0.987890 ms |
| Presenter range | 0.689460–1.328460 ms | 0.684100–1.209650 ms |
| Host idle window | 74.321554 s | 107.236802 s |
| QEMU process host CPU time | 14.84 s | 18.49 s |
| QEMU host CPU / wall time | 19.9673% of one CPU | 17.2422% of one CPU |

The presenter median increased 15.6%, with overlapping ranges; normalized host
CPU cost fell 13.6%. Different elapsed windows are recorded rather than compared
as raw CPU seconds. These small samples and single windows do not establish a
stable regression or improvement. They are debugger-qualified elapsed/host-cost
observations in nested KVM, not native or guest-idle utilization. Source inspection
and GDB establish no recurring engine wake/poll while idle; that is separate
from attributing timing noise to the change.

After host snapshots: start and end.
Monitor `info cpus` maps guest CPU0/1/2/3 to threads
370328/370329/370330/370331. Their host CPU seconds were 8.76/2.87/4.05/2.79,
respectively (8.1688/2.6763/3.7767/2.6017% of one host CPU). No debugger, build
or second VM ran during either cost window. Active-playback CPU cost was not
measured in this bring-up; IRQ/refill/mixer work must measure it later.

### Unmerged consumer and known PCM

Branch **`probe/audio-task1-qualification`**, measured head **`ced1443`**, adds
only a private consumer and its worker invocation over production `9e456bd`.
It is kept unmerged, not a production boot tone or new self-test facility.
Qualifier kernel:
ELF `a5e6c48834fedf46c798d7860db6590772757759636897568d6c647603afbae7`,
ISO `8ba762cb165dd6eeea685a62218a5cf827615da29b71c8a4e7ffe0af255297c4`;
the complete initrd is still identical to baseline.

The consumer submits 310 ordinary vendor reads across the 256-entry rings,
then activates/copies/runs the static 40 ms buffer for about one second, stops
physical DMA, detaches/mutes the converter, reopens 100 ms of zeros, and finally
shuts down stream/rings/link/BME. It polls position every 5 ms only on this
unmerged branch. It never overwrites running DMA or exercises software refill.
Commands during playback are stopped while BME stays enabled for the stream;
BME clears after its stop, then re-enables only for subsequent codec commands.

| QEMU run | Vendor-read ring pointers | Final commands/replies | Tone elapsed including stop/detach | Polled BCIS / observed wraps | Captured tone / zero frames |
| --- | --- | ---: | ---: | --- | --- |
| Four CPUs, `hda-output` | CORB/RIRB 54/54 after 310 reads | 366/366 | 1009.881 ms | 101 / 25 | 48,761 / 4,278 |
| One CPU, `hda-duplex` | CORB/RIRB 54/54 after 310 reads | 376/376 | 1032.612 ms | 100 / 25 | 48,465 / 4,786 |

Serial and GDB records for both runs confirm no engine failure, stopped/reset stream/rings/link, BME off,
retained DMA and no unsolicited replies. LPIB is modulo the cyclic buffer;
polled BCIS and wrap counts can miss/coalesce events. They do not prove precise
hardware-period accounting, audible drain or sustained dynamic refill.
Duplex's unavailable ADC-backend warning is expected for output-only WAV.
The inventory and five echo commands completed after
qualification, confirming guest responsiveness rather than concurrent audio load.

Every captured nonzero frame matches the expected independent channels exactly:
left 1 kHz triangle, peak ±8192, period 48 samples; right 500 Hz triangle, peak
±4096, period 96. For a triangle of amplitude A and period P, phase p=i%P:
`x=-A+floor(p*4*A/P)` for p<P/2, otherwise `x=3*A-floor(p*4*A/P)`.
Both wave periods divide the 1,920-frame cyclic buffer. After the tone, every
frame is zero. Sample comparison used standard Python wave/struct only;
no generated waveform replaces measured output. The output WAV
(not kept in the tree) had SHA-256
`63595f3633d9f3bda93d37b8d3753acbc3ccad5491f3a6afc2ae910df68314b7`
(212,200 bytes, 53,039 frames). Duplex WAV hash
`a4e7970a02781aaafe005beef65c99d641e027cf280f4e7093382d4c914b2462`
(213,048 bytes, 53,251 frames). Both are stereo S16 at 48 kHz.

## Delivery and remaining work

Task 1 settles the documented session contract and delivers the assigned private
controller/codec stage, corresponding to the milestone's contract and engine
checkboxes. The shipped worker initializes quietly and parks. IRQ/refill,
per-space sessions/software mixing, native binding/qualification and consumers
remain unassigned later tasks. The ALC257 data is retained for the native route;
QEMU evidence does not qualify it. Confirm the owner's QEMU-closure/native-batch
choice at milestone closure. All task-owned guests, debuggers, clients and builds
are stopped. Existing exact submitted-head CI is inspected separately from
these measured code/consumer revisions.

## Review follow-up: boot log volume

The review of [#553](https://git.internal/PyxisOS/pyxis-os/pulls/553) requested
one success summary and quiet absence. Controller/codec success details, normal
shutdown and the QEMU pin-control detail now use ktrace. Ambiguous/incomplete
PCI selection and failure explanations remain klog, including failed shutdown
readbacks. The raw measurements above retain the original log output; this
follow-up changes logging severity only, not the qualified transport/stream
sequence. The unmerged qualifier remains at its recorded revision.
