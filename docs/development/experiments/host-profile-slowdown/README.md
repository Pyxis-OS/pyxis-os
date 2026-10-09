# HOST profiler slowdown investigation

Raw captures, transcripts, patches, scripts and screenshots once kept in this directory were removed from the tree; Git history keeps them at `d6733033`.

Task 4 of the [I/O reliability milestone](../../io-reliability-attribution.md)
used a controlled notification × collection experiment. These patches preserve
the original experiment against its recorded baseline. The notification proposal
was subsequently accepted and implemented; see the
[correction and validation](../../io-reliability-attribution.md#host-publication-notification).
The counts-only patches remain **experiment artifacts, not an SDK mode**. Do not use
the counts-only kernel with normal profile consumers: its duration fields are
unavailable, and only the patched iobench reporter describes that correctly.

## Findings

The roughly 15× slowdown reproduced and mostly disappeared when initial HOST
publication notified the BSP. Counts alone did not reproduce it. The timestamp
path changes request timing, and the missing notification amplifies that change;
the observed penalty cannot be treated as a constant cost of reading the clock.

| Initial notification | Collection | Median transfer, ms | Range, ms | Relative to its off control |
| --- | --- | ---: | ---: | ---: |
| Existing behavior | Off | 124.337 | 118.789–128.664 | 1.000× |
| Existing behavior | Counts only | 132.160 | 121.487–137.220 | 1.063× |
| Existing behavior | Full | 1855.850 | 1749.053–1923.083 | 14.926× |
| Explicit BSP notification | Off | 116.313 | 111.245–120.890 | 1.000× |
| Explicit BSP notification | Counts only | 116.876 | 115.540–126.444 | 1.005× |
| Explicit BSP notification | Full | 234.100 | 225.164–253.393 | 2.013× |

The full-profile median falls by 87.4% with notification. The profiling-associated
median excess over its own off control changes from 1731.513 ms to 117.786 ms.
These are descriptive differences between groups, not paired per-request costs
or a basis for subtracting overhead. The off/counts ranges overlap in each
notification condition; these five-sample groups do not resolve a small counter
cost reliably. Counts-only remains useful for checking transfer accounting.

Only the full runs expose phases. Their sums averaged over the five passes are:

| Instrumented interval | Existing behavior, ms/pass | Notification, ms/pass |
| --- | ---: | ---: |
| Preparation/publication | 18.272 | 14.562 |
| Initial BSP queue | 1375.352 | 35.258 |
| Worker queue | 64.137 | 51.388 |
| Worker service | 333.711 | 99.892 |
| Caller resumption | 30.934 | 19.972 |
| Profile total | 1822.405 | 221.071 |
| Transport, contained in service | 297.515 | 69.390 |

Initial queue mean per request falls from 5.331 ms to 0.137 ms; per-pass queue
maxima fall from 8.005–8.097 ms to 1.697–2.528 ms. This is causal evidence that
initial notification removes most of the observed full-profile delay under this
workload. It also shows why changing notification is not merely subtracting one
phase: service, transport observation and other intervals change too. Transport
is nested within service and must not be added to the top-level partition.

Code inspection explains a plausible mechanism. After waking the AP caller, the
BSP can sweep the initial HOST queue and reach `sti; hlt` before the next request
is published. Existing publication sends no IPI, so later publication need not
wake an idle BSP. Its 120 Hz timer provides a subsequent opportunity to sweep the
queue. The near-8 ms maxima are consistent with that mechanism. The caller's
profile completion work and next request's timestamps delay publication and can
move it across the queue sweep. The experiment did not trace BSP idle entry or
the wake interrupt for individual requests, so it does not prove every queue
delay was a timer wait or uniquely identify which timestamp crosses the boundary.

Full profiling adds eight clock reads on an ordinary successful prepared write:
preparation, publication, forwarding, worker start/end, resumption, and transport
submission/completion. The shared HPET clock uses three uncached MMIO reads per
call, with rollover retry and conversion arithmetic. The measured userspace
clock-call loops span 36.076–39.793 microseconds/read. Multiplying by 8 × 258
gives about 74–82 ms as a scale comparison only: syscall calibration is not direct
kernel clock measurement, and contention/interleaving differ. It neither explains
the original 1.7 s excess by simple addition nor measures the residual 118 ms.

The remaining roughly 2× perturbation includes direct timestamp/duration work
and any changed scheduling or transport-completion observation. In particular,
transport time ends when the guest observes the used ring, not when the daemon
finishes. No host-side component timestamps were collected. The service decrease
with an unchanged transport algorithm cannot be attributed to faster host storage.
Counts/full also remove duration arithmetic and branches together with clock
reads, so this design does not isolate pure MMIO latency. Full profiles continue
to describe **instrumented** execution; they cannot partition normal unprofiled
latency, even after the experimental notification.

## Original correction proposal and validation

This section records the task-4 proposal before acceptance; the implementation
and its new measurements are linked above. Historical samples remain unchanged.

Propose a focused permanent change to initial HOST publication: preserve the
saved wait pointer and existing early-wakeup protocol, release the queue lock,
then notify the BSP with the existing helper. Keep profiling, the clock source,
timer and transport behavior unchanged. Do not generalize this result to other
BSP queues or add a collection policy in that PR. The temporary patch is the
concrete candidate, but no runtime change is delivered by this investigation.

Before accepting it, agree validation on ordinary one- and four-CPU boots:
prepared HOST write plus read/copy with matched profile-off/on groups and existing
byte/count verification; ordinary open/close/metadata operations and prompt
error returns; post-run debugger checks for empty queues, released references
and no active profile/transport. Review both early completion before parking and
normal parked wake, including CPU 0's no-self-IPI path. No fault injection or new
test harness is implied. Repeat affected task-3 controls after the correction;
keep these historical measurements intact.

No clock replacement is proposed here: a faster cross-CPU clock needs its own
monotonicity, synchronization and virtualization contract. A permanent counts-only
mode would need explicit ABI/reporting availability semantics; zero duration
fields must never masquerade as measurements. The notification was subsequently
implemented and its affected controls rerun. The milestone closed with further
resolution work and the final combined matrix deferred; no permanent counts-only
mode was accepted. Future comparisons must continue to use profile-off timings.
Normal-workload phase attribution remains unresolved and is recorded in
[technical debt](../../../technical-debt.md#host-file-profiling-perturbation).

## Reproduction contract

The source baseline is Pyxis `0c847c3112186b5dba101a47f365690f66acb940`
and userland `e7fb5ae5c7ee272cd9ad350506883473956a47a6`. The latter is the
merge revision with the same tree as the parent pin
`f86daf40ae2c59e00547cc2eb33c19a31731eebb`. Ports remains
`6ec1290f87882392390e6a889be61ba3ac1448d1`, lwIP
`a1aadb91a50360ff5b52864f7cec810b8162ee85`. No public ABI, clock source,
timer frequency, transport algorithm or benchmark transfer boundary changes.

Apply patches to disposable checkouts of those revisions; paths in the
userland patch are relative to the userland repository. Build each variant with
`make -j16 image` using the existing GCC 16.2.0 toolchain.

| Image | Kernel patches | Userland patch | Runs, in order |
| --- | --- | --- | --- |
| base | none | none | base-off, base-full |
| counts | counts-kernel.patch | counts-userland.patch | base-counts |
| notify | notify-kernel.patch | none | notify-off, notify-full |
| notify-counts | notify-kernel.patch, then counts-kernel.patch | counts-userland.patch | notify-counts |

Each image gets a fresh manual QEMU boot and virtiofsd session with a fresh tmpfs
export. On CPU 1's development shell, run once per configuration:

```sh
iobench write host://NAME --prepared 2> home://NAME.log
iobench write host://NAME --prepared --host-profile 2> home://NAME.log
```

Use the first command only for `*-off`, the second for full/counts, and a unique
NAME for each run. Defaults are a 1 MiB generated fixture, 4080-byte buffer, one
unprofiled warmup and five measured, verified passes. Preparation fills the output
with a contrasting pattern outside the measured window; verification reads it
afterward. Sync is off. HOST BEGIN/END surround the measured transfer; reporting
and RAM log growth occur afterward. No RAM profiling. Keep the existing clock-call
calibration and elapsed transfer clocks in all variants; no overhead subtraction.

The counts patches preserve requested/completed bytes, native and transport
counts, completion classification, saturation and the scoped worker accumulator.
They remove only HOST profile clock reads and duration calculation/accumulation,
including rejection timestamps. Existing transport deadline clocks stay intact.
The reporter explicitly says phase timings are unavailable. Thus counts vs full
isolates the timestamp-and-duration path together, not clock reads alone.

The notification patch saves the wait pointer before publication, releases the
queue lock, then calls the existing remote reschedule helper before sleeping.
It covers all initial HOST operations, not just profiled READ/WRITE. A CPU 0 caller
sends no self-IPI; an AP caller notifies CPU 0. The existing notified/parked
handshake handles completion before sleep. No request storage is read after
publication until the caller has regained ownership. Worker and transport waits
are unchanged. Code review covered these ordering and lifetime constraints.

## Environment and collection

Collected on 2026-09-28 in nested KVM: Fedora 44, Linux
6.19.10-300.fc44.x86_64, exposed Intel i9-12900K, QEMU 10.2.2, Q35, `-cpu max`,
four vCPUs, 256 MiB, GTK display, OVMF from `/usr/share/edk2/ovmf`, network off,
virtio entropy on. Guest HPET period is 10,000,000 fs; the APIC timer stays at
120 Hz. These are not owner-host measurements.

virtiofsd 1.14.0 exports a fresh `/dev/shm` directory through `unshare -Ur`, with
`--sandbox namespace --inode-file-handles=never --no-announce-submounts
--rlimit-nofile=0`. Default cache mode, no writeback or cache eviction. Use the
[ordinary runner](../../../../scripts/run-qemu.sh) with `CPUS=4 ACCEL=kvm
MEMORY=256M VIRTIO_NET=0` and the daemon socket in `VIRTIO_FS_SOCKET`.

Commands were entered interactively; values were manually transcribed from guest
log displays. There is no benchmark/boot automation or new test harness. The
sample set recorded sample order, native counts,
phase sum/max pairs, clock calibration and built ELF/ISO identities. Clock-loop
values measure userspace calls, not isolated kernel clock cost.

All four ordinary builds succeeded. All six warmups and thirty measured passes
verified the entire fixture: 258 writes and 1,048,576 completed bytes per pass,
zero failed passes or short writes. All twenty profiled samples recorded matching
258 native requests and 258 transport submissions/completions, zero native or
transport failures, zero EOF/short transfers, no saturation, and no profiled reads.
Post-run read-only GDB inspection on every boot found empty initial/worker and
cleanup queues, no active profile pointer, zero host lookup references/open
handles, no in-flight transport requests and a ready session. Debugger inspection
was outside all timed work. Experiment QEMU/daemon processes and exports were
removed afterward.

The groups ran sequentially without randomization or repeated independent boots
per cell. There is no statistical confidence claim for small differences between
off/counts cells. The six-cell design isolates two source changes, but each can
alter interleaving throughout the request path. Read/copy, error, timeout,
concurrent-client, single-CPU and owner-host performance were not measured here.
