# SCI, high RAM and HPET recovery

Owner decisions accepted 2026-10-15 (#877): exclude usable RAM above the current
64 GiB physical PMM span with one warning; bound HPET calibration, recovering
with a qualified CPUID 0x15 frequency or failing explicitly if no usable clock
remains. The fatal clock-loss case is an accepted exception to warn-and-fallback.
Horse has 64 GB RAM and is the concrete trigger to revisit full map-sized PMM
coverage together with heap-capacity accounting.

Control: `dcbcb47b`, main `67c68042` plus the never-merged, read-only Vela A/B
observer. Its relevant PMM, SCI routing and clock code is unchanged from main.
Implementation: `782692dd`, based on main `8be086e3`; later record-only commits
do not change these paths. Existing LLVM builder `pyxis-llvm23.1.3-df87791`,
`make -j8 image PREBUILT="sdk userspace ports"`; verified matching #867 bundles
retain fs `101e1f68`, userland `1fce25df` and ports `0e760128`. No ABI or
submodule pin changes. Ordinary source/image builds pass.

QEMU 10.2.2, Q35, nested KVM, host CPU, OVMF, standard VGA, VirtIO RNG, no
writable data disks. Local serial/GDB evidence remains under the worktree's
`build/qualification`; no new self-test, instrumentation or test framework.

| Configuration | Control | Implementation |
| --- | --- | --- |
| 4 CPUs, 2 GiB, `-machine q35,i8042=off` | uACPI SCI install fails; namespace unavailable | SCI 9 level/high enabled, namespace ready, all four live spaces, orderly emulated power-off |
| 4 CPUs, 65 GiB | PMM bitmap-capacity panic | one clipping warning, all spaces, orderly power-off |
| 1 CPU, 2 GiB, normal i8042 | existing supported path | keyboard/mouse bytes consumed, namespace ready, advancing timer IRQ count, no unclaimed SCI, orderly power-off |
| 4 CPUs, 2 GiB, normal i8042 | existing supported path | same routes/namespace and keyboard/mouse progress, orderly power-off |
| 1 CPU, 2 GiB, `-machine q35,hpet=off` | HPET table required by existing contract | same explicit missing-table panic; this is not a stalled-counter test |

The 65 GiB guest uses sparse file-backed RAM and a 6 GiB host cgroup limit,
not 65 GiB resident memory. With fresh OVMF variables and the same image/device
arguments, add:

```sh
systemd-run --user --scope -p MemoryMax=6G qemu-system-x86_64 \
  -machine q35,memory-backend=ram -m 65G \
  -object memory-backend-file,id=ram,size=65G,mem-path=/tmp/pyxis-65g-ram,share=on,prealloc=off \
  -accel kvm -cpu host -smp 4
```

Supply the normal OVMF, ISO, display and console arguments as in
[QEMU usage](../../qemu.md). GDB observes exactly `0x1000000` covered frames
(64 GiB / 4 KiB), 1,024 metadata pages and about 16.216 million allocatable
frames: unsupported upper RAM is excluded and lower RAM is available. QMP
keyboard make/break and relative pointer input produced seven consumed PS/2
bytes on the one-CPU guest. Final source review also checks shared-pin progress
accounting across polling, controller MMIO deduplication and PCI exclusion.

Limits: this host does not expose invariant TSC to KVM (even when requested),
so these boots keep a healthy HPET. Stationary-HPET timeout, CPUID recovery,
provisional BSP/checked-AP time and failed-qualification paths are source-reviewed
only; no fault injection was performed. Shared pins and distinct I/O APICs are
source-reviewed, not native-qualified or exercised by these Q35 tables.
No native result is claimed. The ThinkPad regression packet/run sheet is staged
separately through the coordinator; no native media was written by the agent.
