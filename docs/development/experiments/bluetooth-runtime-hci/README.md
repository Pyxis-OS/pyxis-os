# Bluetooth mouse task 2: runtime HCI transport

Raw captures, transcripts, patches, scripts and screenshots once kept in this directory were removed from the tree; Git history keeps them at `d6733033`.

Status: **task 2 implemented and warm passthrough validated 2026-10-08.**
This is task 2 of the [mouse milestone](../../../wip/bluetooth-mouse.md),
separate from the completed investigation's identically numbered source-assessment
task. The owner authorized production AX200 binding, an exclusive process-owned
controller grant, event/asynchronous ACL reception, kernel-owned command/data
credits and bounded progress/exit/loss behavior.

## Branch and scope

Branch `bluetooth/runtime-hci` starts from fresh main `3bda2c3`, the merge of
[#548](https://git.internal/PyxisOS/pyxis-os/pulls/548). While #548 was open,
preparation used a stack that also included main `ad0db38`; its signed merge
`eacf24f` is preserved on `bluetooth/runtime-hci-before-548-merge`. The work moved
to the fresh-main branch before any kernel implementation. No merge dependency
on #548 remains.

The [accepted contracts](../../../wip/bluetooth-task1-contracts.md) settle the
task's policy. Read-only implementation audits found no further owner choices
needed to begin. Later review exposed the independent-stream handle-reuse limit;
the owner accepted fail-closed reuse for task 2 on 2026-10-08. Connection/reconnect
tasks must qualify the retirement boundary before bonded reconnect is ready.
See the [runtime contract](../../../devices/bluetooth-hci.md). Numeric ABI, capacity and deadline choices remain ordinary
implementation choices under those contracts. New contrary hardware evidence
returns to the owner. The historical warm profile may produce staged development
evidence under this assignment; it is not production firmware qualification.
Cold upload, scan/connection policy, pairing, durable bonds and pointer injection
are later tasks, not assigned here.

Do not block the owning xHCI worker while waiting for HCI completion or userspace
operations. It performs bounded completion work beside storage progress, with
copied inputs and independently retained
ACL receives rather than synchronous bulk-IN polling. Preserve true USB-failure
quarantine. Unconfirmed service cleanup leaves Bluetooth unavailable and does
not independently authorize quarantining storage. Command completion alone
does not prove a radio procedure finished. Historical probes remain unmerged.

## Baseline preparation

The current ThinkPad runs Fedora kernel `6.19.10-300.fc44.x86_64` and QEMU
`10.2.2`, with KVM on the physical host, not nested virtualization. Fedora
Bluetooth is inactive/disabled and rfkill is unblocked. Filtered current-boot
Linux journal fields report firmware revision 0.3, build 193/week 33/2024,
matching the [historical warm observation](../bluetooth-task4/README.md#measured-warm-state).
That is a Linux observation, not a new Pyxis readiness measurement.

Kernel, SDK, userspace and ports bundles were downloaded together from successful
[CI run 1297](https://git.internal/PyxisOS/pyxis-os/actions/runs/1297), revision
`01edf6d49814b6450105eb1d2878363542540d97`, and passed the existing bundle
verifier during image assembly. At baseline capture, code inputs were unchanged from fresh main
`3bda2c3`; its then-current branch differences were documentation. The CI compiler is Clang
23.1.3, fork `49e2c1a1518b3e4687b52ceb6001069c1b6d261e`.

Dependency pins are userspace `b83ff679e91911e9483e24901afca9b3b26d0071`,
ports `a642f07382e14bd233ac1be2b6a814e95c32d835`, filesystem
`b427df29f865bc361b8da92bcd74e114581e9a32` and lwIP
`a1aadb91a50360ff5b52864f7cec810b8162ee85`.

A private 68 MiB GPT disk holds a 64 MiB npfs `system` volume with an 8 MiB
journal, the existing 1 MiB `iobench` fixture and a boot override that grants
the volume read-only to a single network-owning space. Each run uses a fresh
private writable disk copy: the packaged installed defaults initially open
`system` read-write before reading the override, so physical read-only
attachment cannot bootstrap this fixture. The workload itself performs reads.
The original fixture remains unattached and unchanged. No physical storage is
used. Ordinary `mkfs.npfs` and `fsck.npfs` prepared/checked the volume, and
`sfdisk` prepared the disposable regular-file GPT.

The preparation guest used Q35, four CPUs, 2 GiB, fresh Fedora raw OVMF variables,
ISO boot, display disabled, VirtIO RNG/network and `qemu-xhci` with the private
disk at port 2. It had **no AX200 attachment**. Guest `lsusb -n`, fixture text
reading and `iobench read system://iobench.bin --buffer 65536 --rounds 3`
completed successfully; all three 1 MiB samples verified. This only establishes
that the intended storage workload works. It is not the requested coexistence
baseline or an attached-controller CPU/interrupt measurement.

## Attached-controller baseline

Captured 2026-10-08 with the unchanged CI kernel above, before runtime code
changes. Three fresh guests used the same preparation configuration, with
AX200 `8087:0029` added at xHCI port 1 and storage at port 2. Attachment was
restricted to the verified USB bus/device node; it remained `/dev/bus/usb/004/003`
through all three runs. Fedora Bluetooth remained inactive/disabled. QEMU's
ordinary libusb attachment detached the host interfaces and rebound them on exit.
The separate LLVM process tree was stopped before storage and idle measurements
and resumed only after the last guest exited; no build work ran during measurement.

Each guest ran the existing workload through the ordinary remote shell:

```text
lsusb -n
cat system://README.txt
iobench read system://iobench.bin --buffer 65536 --rounds 3
exit
```

All commands succeeded, AX200 and storage appeared in complete inventory, and
one warmup plus three 1 MiB read samples verified in every boot. The three
samples within a boot are repeated warm reads through the existing npfs cache,
not independent cold-media trials. The controller was running and not failed.

| Boot | Payload read samples (ms) | Complete consumption samples (ms) |
| --- | --- | --- |
| 1 | 115.952, 123.140, 123.701 | 117.696, 124.750, 126.093 |
| 2 | 112.814, 117.644, 120.521 | 114.727, 120.753, 122.258 |
| 3 | 112.298, 120.415, 118.857 | 114.210, 121.589, 120.299 |

Across nine samples, payload median/range was **118.857 / 112.298–123.701 ms**;
complete-consumption median/range was **120.753 / 114.210–126.093 ms**. Profiling
was disabled. These figures establish the matched workload, not native storage
throughput or concurrent radio-traffic qualification.

After the remote workload exited, each boot measured one 30-second idle interval.
GDB inspected existing xHCI counters and per-CPU timer counts at the boundaries,
detaching before the timed interval. `/proc/PID/stat` supplied whole-QEMU
user/system CPU ticks at 100 ticks/s; host monotonic time supplied elapsed time.
The CPU percentage is relative to **one host CPU**, includes the entire guest
and QEMU background work, and does not isolate the Bluetooth function.

| Boot | Actual interval (s) | QEMU user/system CPU (s) | One-CPU cost | xHCI IRQ/command/event deltas | Timer deltas, BSP/AP1/AP2/AP3 |
| --- | ---: | --- | ---: | --- | --- |
| 1 | 30.000514 | 3.75 / 3.93 | 25.60% | 0 / 0 / 0 | 7966 / 3611 / 3612 / 3612 |
| 2 | 30.000082 | 3.32 / 3.65 | 23.23% | 0 / 0 / 0 | 7080 / 3612 / 3612 / 3612 |
| 3 | 30.000105 | 3.34 / 4.13 | 24.90% | 0 / 0 / 0 | 8095 / 3612 / 3612 / 3612 |

Idle cost median/range was **24.90 / 23.23–25.60%** of one CPU. All intervals
observed zero additional xHCI IRQs, commands and events; ordinary guest timer
activity continued. Tick resolution is 10 ms and ambient Fedora/QEMU scheduling
was not isolated. BSP timer variation and whole-process CPU variation are
observations, not evidence that Bluetooth caused either. No HCI binder or
posted Bluetooth receive stream exists in this baseline.

Counter expressions used the matching saved ELF and no inferior calls:

```text
p 'kernel/usb/xhci.c'::controllers->running
p 'kernel/usb/xhci.c'::controllers->failed
p 'kernel/usb/xhci.c'::controllers->interrupts
p 'kernel/usb/xhci.c'::controllers->commands_completed
p 'kernel/usb/xhci.c'::controllers->events_consumed
p cpus[0]->timer_interrupts
p cpus[1]->timer_interrupts
p cpus[2]->timer_interrupts
p cpus[3]->timer_interrupts
```

The kernel, SDK, userspace and ports manifests matched the same inputs.
Repeat the same workload after implementation, with no compiler/build workload
during measurement. Real ACL traffic remains task 4's qualification gate. No
new benchmark or test infrastructure was added.

## Runtime implementation and initial warm check

The [runtime reference](../../../devices/bluetooth-hci.md) describes the native
process-owned grant, bounded framing, kernel credits and retained USB transport.
No userspace service or connection procedure is added by task 2. The public ABI
change required new matching SDK, userspace and ports bundles; dependency pins
remain unchanged.

An initial interactive warm boot used matching bundles from successful CI run
1322 at `b896bc4`, with the baseline configuration and fresh disk/OVMF copies.
Scalar GDB inspection found initialization ready (stage 11), complete sealed
inventory, one attachment, no owner/terminal failure, HCI version 11/revision
8641, LE features `80059ff`, validated mandatory LE commands, one command credit,
ACL payload length 251 and three available/total ACL packet credits. Ten event
USB completions supplied the initialization replies; neither stream had a partial
frame. Both interrupt and bulk receive slots remained posted, with empty copied
queues. The guest exited. This is framing/readiness and idle ownership evidence,
not actual ACL payload traffic or a timed post-change comparison.

Integration review corrected late retired-link ACL handling, post-callback USB
failure reporting, HCI request progress during storage waits, singleton candidate
counting before transport admission, and bounded first-link ACL deferral across
independent event/ACL drains. The latter retains eight whole frames in order for
at most five seconds from their first byte, with captured epoch/generation and
explicit overflow/expiry loss. Current cleanup is deliberately conservative: radio-changing command publication,
connection admission or ACL publication permanently taints the session, so
release/exit requires reboot even after credits/links settle. Read-only sessions
can be re-granted only after fully confirmed accounting. Current HCI BSS storage
is 214344 bytes; the new
private async bulk pool reserves five DMA pages per controller plus copied
metadata, independently of the storage budget. These are current implementation
settings, not public capacity guarantees.

The owner accepted fail-closed handle reuse on 2026-10-08. The
[reuse boundary debt](../../../technical-debt.md#bluetooth-hci-connection-handle-reuse-boundary)
remains a prerequisite to bonded reconnect. Real connection/ACL traffic,
command/data credit recycling, source loss under traffic and process acquisition/
release/exit through a userspace consumer have not been measured by this idle
check. Framing/credits/lifetime error paths are source-reviewed; task 4 adds the
actual service/connection consumer and its qualification. No fault injection,
new tests or benchmark infrastructure were added.

## Matched runtime-code validation

Three fresh interactive guests used matching bundles from successful
[CI run 1326](https://git.internal/PyxisOS/pyxis-os/actions/runs/1326), signed code
revision `8e67c3c9b1c4b359131ad13515bdf6c2e1111b95`. QEMU configuration,
compiler fork, disk fixture, workload and idle sampling matched the baseline;
only the kernel/public ABI and corresponding SDK content changed. The existing
bundle verifier accepted all components. No compiler work ran during measurements.

All three guests reached development readiness with the same checked firmware,
features and credits as the initial warm check. Both receive slots on each
endpoint remained posted, with no partial HCI frame, copied queue backlog or
terminal failure. The parsed firmware/size snapshot
recorded the operational tuple, zero deferred frames and the clean idle state;
no packet or peer identity was dumped.

All commands, warmups and nine storage samples verified successfully. The three
private disk copies retained the original hash after each guest exited.

| Boot | Payload read samples (ms) | Complete consumption samples (ms) |
| --- | --- | --- |
| 1 | 116.207, 125.406, 125.356 | 118.074, 126.787, 126.836 |
| 2 | 114.352, 121.789, 122.615 | 116.089, 123.336, 125.407 |
| 3 | 113.906, 119.625, 122.716 | 115.348, 122.508, 123.667 |

| Boot | Actual interval (s) | QEMU user/system CPU (s) | One-CPU cost | xHCI IRQ/command/event deltas | Timer deltas, BSP/AP1/AP2/AP3 |
| --- | ---: | --- | ---: | --- | --- |
| 1 | 30.000446 | 3.90 / 4.07 | 26.57% | 0 / 0 / 0 | 7988 / 3618 / 3618 / 3618 |
| 2 | 30.000800 | 3.58 / 4.09 | 25.57% | 0 / 0 / 0 | 7800 / 3618 / 3619 / 3619 |
| 3 | 30.000249 | 3.44 / 3.86 | 24.33% | 0 / 0 / 0 | 6129 / 3618 / 3618 / 3618 |

| Measure | Baseline median / range | Runtime median / range |
| --- | --- | --- |
| Payload (ms) | 118.857 / 112.298–123.701 | 121.789 / 113.906–125.406 |
| Complete consumption (ms) | 120.753 / 114.210–126.093 | 123.336 / 115.348–126.836 |
| Whole-QEMU CPU (% of one CPU) | 24.90 / 23.23–25.60 | 25.57 / 24.33–26.57 |

Payload median increased 2.5%, complete-consumption median 2.1%, and whole-QEMU
CPU median 0.67 percentage points (2.7% relative). Ranges overlap. Three boots
and cached reads with ambient host scheduling cannot attribute those differences
to Bluetooth or establish cost equivalence. Source inspection shows additional
bounded bookkeeping in existing worker ticks; no new polling interval was added.
Measured idle IRQ/event cost was zero despite posted receive DMA. Memory cost
is explicit: HCI BSS 214344 bytes, async bulk metadata 33152 bytes and five DMA
pages per controller, separate from existing storage/interrupt resources. Active
radio and concurrent storage remain later measurement gates.

One preliminary boot-1 idle capture reported 26.30% CPU, but manual tool-call
gaps extended its debugger counter interval beyond its CPU window. It was
excluded from the matched table and repeated in the same guest. Final captures
invoke GDB-before, the existing 30-second `/proc` sample, and GDB-after directly
in one shell invocation, keeping debugger pauses outside the CPU window. Timer
snapshots include the small boundary overhead; they are not an exact timer rate.
No sample was selected based on its performance value.

The final kernel, SDK, userspace and ports manifests recorded the
revision/configuration and unchanged dependency pins. Image assembly used:

```sh
make -j16 image PREBUILT="kernel sdk userspace ports" \
  INIT=/tmp/pyxis-bluetooth-runtime-baseline/init.sh \
  MOUNT_DISK="$(cat /tmp/pyxis-bluetooth-runtime-baseline/disk-guid.txt)"
```

The QEMU command follows the baseline configuration: Q35/KVM, `-cpu max`,
`-smp 4 -m 2G`, fresh Fedora OVMF variables, the assembled ISO, no display,
VirtIO RNG/network, `qemu-xhci`, verified `usb-host` AX200 at port 1 and a fresh
private `usb-storage` copy at port 2. Remote workload commands above are unchanged.
Scalar inspection uses the matching ELF, the baseline counter expressions and
named HCI stage/features/credit/stream fields, with no inferior calls.

| Matched runtime artifact | SHA-256 |
| --- | --- |
| ELF | `82c78d64c7c256a17363a947acf33312a1f9af229c965a3b26ce81909bfeeb6c` |
| ISO | `e822994efed741f32bc42ea39972beeef0501681bb8dd582114fdea457c354c8` |

Task 2's event framing, warm readiness, retained idle event/ACL reception and
storage coexistence are measured. Acquisition/release/process-exit syscall paths,
real ACL payload/credit recycling, first-link deferral and handle reuse/error
paths remain source-reviewed; task 4 supplies the actual service and connection
qualification. No native Pyxis Bluetooth readiness is claimed. Cold upload,
SMP, durable bonds, HID and pointer are still unassigned tasks.

## Main integration

Signed merge `ca4d0f602019ec9b254d6cb73452b2216ff5383f` integrates published main
`0179163`. Main had added terminal-pointer protocol 43/object 45; Bluetooth now
uses protocol 44/object 46, preserving both typed dispatch paths and both audio/
Bluetooth source discovery. This renumbers an unmerged ABI, without a compatibility
shim. Bluetooth transport/framing code is unchanged from the matched revision.
Main's published userspace pin is `63d4324d05762992a09ed8768f6e9ba4586355e2`;
no dependency repository was edited and other pins remain unchanged.

The integrated ordinary kernel build passed. All jobs in
[CI run 1332](https://git.internal/PyxisOS/pyxis-os/actions/runs/1332) passed;
its matching bundles passed the existing verifier during image assembly. One
fresh interactive warm guest repeated the same configuration, remote read
workload and 30-second idle procedure. The storage output
verified its warmup and all three samples: payload median/range
120.977 / 115.190–121.294 ms, complete consumption
123.359 / 117.086–124.565 ms. The private disk retained its original hash.
The idle snapshot found development-ready stage 11,
complete inventory, the same checked features/credits, no partial frames or
terminal failure, and both receives posted on both endpoints. Whole-QEMU CPU
was 24.70% of one CPU over 30.000749 seconds; IRQ/command/event deltas were zero.
This single integration check is separate from the matched comparison: main's
other kernel/userspace changes cannot be attributed to Bluetooth.

The integrated kernel, SDK, userspace and ports manifests recorded their exact inputs.

| Integrated artifact | SHA-256 |
| --- | --- |
| ELF | `92eb075e57ac24691347c20b17b4bb0b2521b688125cc1963b5fb824ab8d68d0` |
| ISO | `4a69f758bd3b2f2acd653c7f8e83b0754f2497c9e3ad42970bfc741ec6f1c3b1` |

No runtime acquisition/connection/ACL payload or native qualification is added
by this integration check. Its guest exited, with Fedora Bluetooth still
inactive/disabled and the AX200 node unchanged. Integrated artifacts are retained
in `/tmp/pyxis-bluetooth-runtime-validation/ca4d0f6`.

## Recoverable handoff

Preparation artifacts live outside the repository in
`/tmp/pyxis-bluetooth-runtime-baseline`: verified component tar files, `init.sh`,
`disk-guid.txt`, immutable `storage.raw`, its source/pool and preparation logs.
Copies of the matching `caelum.elf` and `pyxis.iso` are preserved there before
later source builds overwrite the build directory:

| Artifact | SHA-256 |
| --- | --- |
| ELF | `d2b0878baad1a0f8a22825de7e1966ee3c102a1ad7e4486d86ea9a0d1ae79572` |
| ISO | `e2b46850138d22e7d3bd2ab126b84c63d51670a028473adc75abb31997526451` |
| Original storage disk | `7a8047155d7169c55374c4a3d47b92a98501280892be853ad0b300131fd75b06` |

The owner granted `chronium` read/write access to USB node
`/dev/bus/usb/004/003`, resolving the earlier password/access blocker. Verify its
identity before attachment; a changed node returns to the owner for access,
rather than blind retries. The attached baseline, runtime implementation,
final-code warm validation and matched measurements are complete.
PR [#552](https://git.internal/PyxisOS/pyxis-os/pulls/552) delivers task 2; later
tasks still require explicit assignment.

The pinned local compiler completed successfully at
`/home/chronium/opt/pyxis-llvm-49e2c1a`, preserving the older prefix. Its recorded
fork is `49e2c1a1518b3e4687b52ceb6001069c1b6d261e`; a clean ordinary
`make -j16 kernel` passed with it. No compiler container rebuild is assigned.
Matching CI SDK/userspace/ports bundles are required for the changed public ABI;
baseline artifacts cannot stand in for those inputs.

Preparation, the initial runtime check and all baseline/final guests exited;
no QEMU, GDB, passthrough or probe process remains. Final files are retained in
`/tmp/pyxis-bluetooth-runtime-validation/8e67c3c`, separate from the baseline.
Fedora Bluetooth stays inactive/disabled. No address, private packet or key was
recorded. Task 2 is complete within its recorded measurement limits. Later tasks
remain unassigned. The ThinkPad is free in Fedora with Bluetooth disabled.

The approval review of #552 at `78f4fbd` requested a logging-only follow-up:
successful initialization emits one `Bluetooth HCI: AX200 USB ready (development
firmware)` summary. HCI version/revision, firmware/features/credits and successful
USB transport details use `ktrace`; failure diagnostics remain `klog`. The
ordinary kernel build passed for this follow-up. Passthrough measurements above
retain their recorded revisions; no further guest was started for the log change.
The branch is parked at the owner's request. Task 3 remains unassigned.
