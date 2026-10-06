# Core-placement checks

Recorded 2026-10-07 for the [topology-aware tie-break](../../../kernel/smp.md#placement-and-migration).
Kernel base: main `ebe6cdf`; changed code adds CPUID core records and the accepted
idle-siblings tie-break. SDK, userspace, ports, filesystem and lwIP inputs match.
Native ThinkPad confirmation remains pending.

## Build and QEMU configuration

Ordinary `make -j16 image PREBUILT='sdk userspace ports'` builds passed without
warnings. The kernel was rebuilt for each code variant. Independently verified
SDK/application/ports bundles had unchanged inputs; no compiler-container rebuild
is needed. The baseline ELF SHA-256 was
`de6a401c124d0c41f6ebfad77e0139f45ae946ecaf876249e13900762a3a2ae6`;
the measured changed ELF was
`6c54d61700305c904b15c568e4f5d375608faa70d387097451800063144b076f`.

The development host is a Fedora 44 KVM guest, Linux 6.19.10, advertising 16
vCPUs, no virtual SMT and an i9-12900K. Its nested KVM `halt_poll_ns` is 200000.
QEMU 10.2.2 (the existing AHCI fix), OVMF, KVM, 512 MiB, default boot config,
virtio-net/rng and TCP forwarding 2423:2323 were held fixed. There were no raw
disks or host block-device accesses. QEMU SMT changes advertised topology; it
does not bind the vCPU threads onto physical host SMT siblings, so these timings
cannot establish native SMT speedup.

## Topology and publication inspection

Eight logical CPUs configured as four cores with two threads report:

| CPU/APIC IDs | Core key | SMT width |
| --- | ---: | ---: |
| 0, 1 | 0 | 1 |
| 2, 3 | 1 | 1 |
| 4, 5 | 2 | 1 |
| 6, 7 | 3 | 1 |

The standard `max` KVM model exercised leaf 0xB. A separate
`EPYC,level=10,topoext=on,vendor=AuthenticAMD` boot exercised the AMD fallback
with the same pairs and completed a remote `ls`/exit normally. KVM's default
vendor override otherwise retained GenuineIntel; capped-basic-leaf boots then
reported all CPUs unknown/isolated. Unsupported host AMD feature warnings were
not performance measurements. With `THREADS=1`, four CPUs reported four distinct
keys with width zero. An invalid THREADS divisor was rejected before launch.

Read-only GDB at four-member pipeline publication showed compute tasks targeting
CPUs 1, 2, 4 and 6: four distinct cores. GDB also observed later running tasks on
CPUs 1, 2, 3 and 6. A hardware watchpoint identified `requeue_preempted()` moving
the CPU-4 task to CPU 2, joining CPU 3 on core 1. These observations distinguish
initial placement from later balancing, which the brief keeps unchanged. They
do not establish a guarantee that running compute tasks remain on separate cores.
No diagnostic code, GDB command files or traces are committed.

## Matched allocation batches

The existing [smp8-check.py](../smp-task8/smp8-check.py) ran unmodified. Each series
warms up pages once, then starts the recorded background batches in fresh remote
sessions. Heap uses 262144 rounds, pages uses 2048. Every series reported zero
failures. Values below are aggregate wall seconds, including launch/completion
traffic; each client also verified its allocations.

The first four-CPU pair ran baseline then changed; the repeat ran changed then
baseline. The eight-CPU pair used four advertised SMT cores. Debugger sessions
were detached before timing.

| Batch | Base 4 | Changed 4 | Base 4 repeat | Changed 4 repeat | Base 8 SMT | Changed 8 SMT |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| heap ×1 | 0.846 | 0.853 | 0.937 | 0.865 | 0.851 | 0.900 |
| heap ×4 | 0.900 | 0.935 | 0.945 | 0.929 | 0.886 | 0.896 |
| heap ×8 | 1.816 | 1.862 | 1.983 | 1.876 | 1.003 | 1.038 |
| heap ×11 | 2.475 | 2.625 | 2.579 | 2.587 | 1.422 | 1.353 |
| pages ×1 | 0.199 | 0.200 | 0.197 | 0.204 | 0.206 | 0.201 |
| pages ×2 | 0.323 | 0.339 | 0.340 | 0.335 | 0.341 | 0.290 |
| pages ×4 | 0.476 | 0.469 | 0.483 | 0.462 | 0.389 | 0.388 |
| pages ×8 | 0.886 | 0.950 | 0.930 | 0.921 | 0.577 | 0.558 |

Several initial small slowdowns reversed in the four-CPU repeat. For example,
heap ×4 moved from 0.900/0.935 s to 0.945/0.929 s (base/changed), and pages ×8
from 0.886/0.950 s to 0.930/0.921 s. The baseline's solo heap wall itself varied
from 0.846 to 0.937 s. These samples do not isolate a repeatable slowdown from
run/boot variation; they also make no claim of native SMT improvement.

The short task-8 allocation workloads also ran as three fresh processes
per mode on the same four-CPU, one-thread-per-core configurations. Internal
elapsed milliseconds (median, range), with zero failures:

| Workload | Base | Changed |
| --- | ---: | ---: |
| `heap` | 12.626 (12.614–12.736) | 12.512 (12.486–12.514) |
| `growth` | 8.265 (8.159–8.282) | 8.314 (8.152–12.935) |
| `pages` | 4.869 (4.844–4.874) | 4.836 (4.833–4.854) |

## Native ThinkPad check (owner-run)

- [ ] Boot the PR's image on the T14 with the default all-CPU configuration and
  SMT enabled. Record the source revision/kernel identity and CPU topology lines.
  Expect 12 logical CPUs and sibling pairs 0–1, 2–3, 4–5, 6–7, 8–9 and 10–11.
  Their expected core keys are 0, 1, 2, 4, 5 and 6; gaps are valid.
- [ ] Run the existing checker from the desktop against the native remote server,
  substituting its actual IPv4 address if needed:

  ```sh
  mkdir -p build/core-placement-native
  cd build/core-placement-native
  python3 ../../docs/development/experiments/smp-task8/smp8-check.py \
    ../tools/pyxis-remote core-placement-t14 192.168.0.50 2323
  ```

- [ ] Return the boot topology lines and `smp8-core-placement-t14.txt`. Record
  heap ×4, ×8 and ×11 against the [task-8 native record](../smp-task8/README.md#native-thinkpad-check-owner-run):
  2.352, 3.598 and 4.432 s wall respectively. Heap ×4 near the roughly 1.3 s
  solo-client time is the owner's expectation, not a measured result here.
  Confirm zero failures and compare ×8/×11 for regression.

If the native results do not show the expected benefit, discuss a separate
balancing follow-up before changing the accepted push threshold or pull policy.
