# HOST profiler slowdown investigation

Raw captures, transcripts, patches, scripts and screenshots once kept in this directory were removed from the tree; Git history keeps them at `d6733033` (the experiment's disposable kernel and userland patches and the machine-readable samples are there).

Task 4 of the [I/O reliability milestone](../../io-reliability-attribution.md) used a controlled
notification × collection experiment. The notification fix was subsequently accepted and implemented; see
the [correction and validation](../../io-reliability-attribution.md#host-publication-notification). The
counts-only patches were **experiment artifacts, not an SDK mode**: that kernel's duration fields are
unavailable, so it must not be used with normal profile consumers.

## Findings

The roughly 15× slowdown reproduced and mostly disappeared when initial HOST publication notified the BSP.
Counts alone did not reproduce it. The timestamp path changes request timing and the missing notification
amplifies that change, so the observed penalty cannot be treated as a constant cost of reading the clock.

| Initial notification | Collection | Median transfer, ms | Range, ms | Relative to its off control |
| --- | --- | ---: | ---: | ---: |
| Existing behavior | Off | 124.337 | 118.789–128.664 | 1.000× |
| Existing behavior | Counts only | 132.160 | 121.487–137.220 | 1.063× |
| Existing behavior | Full | 1855.850 | 1749.053–1923.083 | 14.926× |
| Explicit BSP notification | Off | 116.313 | 111.245–120.890 | 1.000× |
| Explicit BSP notification | Counts only | 116.876 | 115.540–126.444 | 1.005× |
| Explicit BSP notification | Full | 234.100 | 225.164–253.393 | 2.013× |

The full-profile median falls by 87.4% with notification, and the profiling-associated excess over its own
off control changes from 1731.513 ms to 117.786 ms. These are descriptive differences between groups, not
paired per-request costs or a basis for subtracting overhead. The off and counts ranges overlap in each
condition, so five-sample groups do not resolve a small counter cost; counts-only stays useful for checking
transfer accounting. Only the full runs expose phases (sums averaged over the five passes):

| Instrumented interval | Existing behavior, ms/pass | Notification, ms/pass |
| --- | ---: | ---: |
| Preparation/publication | 18.272 | 14.562 |
| Initial BSP queue | 1375.352 | 35.258 |
| Worker queue | 64.137 | 51.388 |
| Worker service | 333.711 | 99.892 |
| Caller resumption | 30.934 | 19.972 |
| Profile total | 1822.405 | 221.071 |
| Transport, contained in service | 297.515 | 69.390 |

The initial queue mean per request falls from 5.331 ms to 0.137 ms and per-pass queue maxima from
8.005–8.097 ms to 1.697–2.528 ms. This is causal evidence that initial notification removes most of the
full-profile delay under this workload; service, transport observation and other intervals change too, so
the change is not merely one subtracted phase, and transport (nested within service) must not be added to the
top-level partition.

The plausible mechanism, from code inspection: after waking the AP caller the BSP can sweep the initial HOST
queue and reach `sti; hlt` before the next request is published, and publication sends no IPI, so a later
request need not wake an idle BSP until its 120 Hz timer (consistent with the near-8 ms maxima). The caller's
profile completion work and the next request's timestamps delay publication and can move it across the sweep.
BSP idle entry and the wake interrupt were not traced per request, so this does not prove every delay was a
timer wait or identify which timestamp crosses the boundary.

Full profiling adds eight clock reads to a successful prepared write (preparation, publication, forwarding,
worker start/end, resumption, transport submission/completion). The shared HPET clock costs three uncached
MMIO reads per call plus rollover retry and conversion; measured userspace clock-call loops span
36.076–39.793 µs/read, so 8 × 258 reads would be about 74–82 ms as a scale comparison only (syscall
calibration is not direct kernel clock measurement). It neither explains the original 1.7 s excess by simple
addition nor measures the residual 118 ms. The remaining roughly 2× perturbation includes timestamp and
duration work and any changed scheduling or transport-completion observation; transport time ends when the
guest observes the used ring, not when the daemon finishes, and no host-side component timestamps were
collected. The service decrease with an unchanged transport algorithm cannot be attributed to faster host
storage, and counts versus full removes duration arithmetic and branches together with the clock reads, so
the design does not isolate pure MMIO latency. Full profiles describe **instrumented** execution and cannot
partition normal unprofiled latency, even after the notification.

## Correction and validation contract

The accepted change preserves the saved wait pointer and the early-wakeup protocol for initial HOST
publication, releases the queue lock, then notifies the BSP with the existing remote reschedule helper
before sleeping. It covers all initial HOST operations (not just profiled READ/WRITE); a CPU 0 caller sends no
self-IPI and an AP caller notifies CPU 0; the notified/parked handshake covers completion before sleep and no
request storage is read after publication until the caller regains ownership. Profiling, the clock source,
timer and transport behavior are unchanged, and the result is not generalized to other BSP queues or to a
collection policy. Validation was agreed as ordinary one- and four-CPU boots with matched profile-off/on
groups and byte/count verification, ordinary open/close/metadata operations and prompt error returns, and
post-run debugger checks for empty queues, released references and no active profile or transport, with review
of early completion before parking, normal parked wake and CPU 0's no-self-IPI path; the affected task-3
controls were repeated after the correction. No clock replacement is proposed (a faster cross-CPU clock needs
its own monotonicity, synchronization and virtualization contract), and a permanent counts-only mode would need
explicit ABI/reporting availability semantics, since zero duration fields must never masquerade as
measurements. No permanent counts-only mode was accepted, future comparisons must use profile-off timings, and
normal-workload phase attribution remains unresolved
([technical debt](../../../technical-debt.md#host-file-profiling-perturbation)).

## Reproduction contract

Source baseline: Pyxis `0c847c3112186b5dba101a47f365690f66acb940`, userland
`e7fb5ae5c7ee272cd9ad350506883473956a47a6` (the merge with the same tree as the parent pin
`f86daf40ae2c59e00547cc2eb33c19a31731eebb`), ports `6ec1290f87882392390e6a889be61ba3ac1448d1`, lwIP
`a1aadb91a50360ff5b52864f7cec810b8162ee85`. No public ABI, clock source, timer frequency, transport algorithm
or transfer boundary changed. The experiment used disposable checkouts built with `make -j16 image` (GCC
16.2.0) in four images: base (no patches), counts (counts-only kernel and userland patches), notify (the
notification patch) and notify-counts (both), with runs base-off/base-full, base-counts, notify-off/notify-full
and notify-counts.

Each image got a fresh manual QEMU boot and virtiofsd session on a fresh tmpfs export. On CPU 1's development
shell, once per configuration:

```sh
iobench write host://NAME --prepared 2> home://NAME.log
iobench write host://NAME --prepared --host-profile 2> home://NAME.log
```

The first command was used only for `*-off`, the second for full and counts, with a unique NAME each. Defaults
were a 1 MiB generated fixture, 4080-byte buffer, one unprofiled warmup and five measured verified passes;
preparation fills the output with a contrasting pattern outside the measured window, verification reads it
afterward, sync is off, HOST BEGIN/END surround the measured transfer, and reporting and RAM log growth happen
afterward. The existing clock-call calibration and elapsed transfer clocks stayed in all variants, with no
overhead subtraction.

The counts patches kept requested/completed bytes, native and transport counts, completion classification,
saturation and the scoped worker accumulator, and removed only HOST profile clock reads and duration
calculation and accumulation (including rejection timestamps); existing transport deadline clocks stayed, and
the reporter said phase timings were unavailable. Counts versus full therefore isolates the
timestamp-and-duration path together, not clock reads alone.

## Environment and collection

Collected on 2026-09-28 in nested KVM: Fedora 44, Linux 6.19.10-300.fc44.x86_64, exposed Intel i9-12900K, QEMU
10.2.2, Q35, `-cpu max`, four vCPUs, 256 MiB, GTK display, OVMF from `/usr/share/edk2/ovmf`, network off,
virtio entropy on; guest HPET period 10,000,000 fs and APIC timer 120 Hz. These are not owner-host
measurements. virtiofsd 1.14.0 exported a fresh `/dev/shm` directory through `unshare -Ur` with `--sandbox
namespace --inode-file-handles=never --no-announce-submounts --rlimit-nofile=0` and default cache mode, using
the [ordinary runner](../../../../scripts/run-qemu.sh) with `CPUS=4 ACCEL=kvm MEMORY=256M VIRTIO_NET=0` and
`VIRTIO_FS_SOCKET`. Commands were entered interactively and values transcribed from guest log displays, with
no benchmark or boot automation or new harness. Clock-loop values measure userspace calls, not isolated
kernel clock cost.

All four builds succeeded. All six warmups and thirty measured passes verified the whole fixture (258 writes
and 1,048,576 completed bytes per pass, no failed passes or short writes), and all twenty profiled samples
recorded 258 native requests and 258 transport submissions/completions with no native or transport failures,
EOF/short transfers, saturation or profiled reads. Post-run read-only GDB checks on every boot found empty
initial, worker and cleanup queues, no active profile pointer, zero host lookup references and open handles, no
in-flight transport requests and a ready session, outside all timed work. The groups ran sequentially without
randomization or repeated independent boots per cell, so no statistical confidence is claimed for small
off/counts differences, and each of the two source changes can alter interleaving throughout the request path.
Read/copy, error, timeout, concurrent-client, single-CPU and owner-host performance were not measured here.
