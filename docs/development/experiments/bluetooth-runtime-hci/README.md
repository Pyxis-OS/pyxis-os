# Bluetooth mouse task 2: runtime HCI transport

Status: **assigned 2026-10-08; attached-controller baseline pending; no code
changes yet.** This is task 2 of the [mouse milestone](../../../wip/bluetooth-mouse.md),
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
needed to begin. Numeric ABI, capacity and deadline choices remain ordinary
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
verifier during image assembly. Code inputs are unchanged from fresh main
`3bda2c3`; the branch differences are documentation. The CI compiler is Clang
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

Pending matched baseline: add the AX200 by VID/PID at xHCI port 1, repeat the
storage workload across three fresh boots and record variation. For idle cost,
observe whole-QEMU user/system CPU time and existing guest xHCI interrupt/event
and per-CPU timer counters across three idle intervals. Keep GDB stops outside
timed intervals and record actual elapsed time. No compiler/build workload may
run during measurement. Repeat matched workloads after implementation; real ACL
traffic remains task 4's qualification gate. No new benchmark or test
infrastructure is authorized.

## Recoverable handoff

Preparation artifacts live outside the repository in
`/tmp/pyxis-bluetooth-runtime-baseline`: verified component tar files, `init.sh`,
`disk-guid.txt`, immutable `storage.raw`, its source/pool and preparation logs.
`build/caelum.elf` and `build/pyxis.iso` currently match that baseline:

| Artifact | SHA-256 |
| --- | --- |
| ELF | `d2b0878baad1a0f8a22825de7e1966ee3c102a1ad7e4486d86ea9a0d1ae79572` |
| ISO | `e2b46850138d22e7d3bd2ab126b84c63d51670a028473adc75abb31997526451` |
| Original storage disk | `7a8047155d7169c55374c4a3d47b92a98501280892be853ad0b300131fd75b06` |

USB node `/dev/bus/usb/004/003` currently identifies the AX200, but lacks user
write access. `sudo -n` returned `sudo: a password is required`. The owner was
asked to grant `chronium` read/write access with `setfacl`; refresh node identity
after any re-enumeration. No passthrough or attached-controller measurement has
run, and kernel implementation waits for that baseline.

The local installed compiler still uses fork `41ab6043`; the pinned `49e2c1a`
toolchain is being prepared separately through the existing owner-hosted source
and `toolchain/build.sh`, preserving the old compiler. Prefix:
`/home/chronium/opt/pyxis-llvm-49e2c1a`; work directory:
`/home/chronium/.cache/pyxis-llvm-build-49e2c1a`; log:
`/tmp/pyxis-bluetooth-runtime-toolchain.log`. Finish/check that preparation before
measurements and source builds. No compiler container rebuild is assigned.

Both preparation QEMU guests exited; no QEMU, GDB, passthrough or probe process
remains. Fedora Bluetooth stays inactive/disabled. No address, private packet
or key was recorded. Task 2 remains unchecked and implementation is unfinished.
