# Runtime SMP task-1 baseline

Raw captures, transcripts, patches, scripts and screenshots once kept in this directory were removed from the tree; Git history keeps them at `d6733033`.

Pre-implementation baseline for the [runtime SMP milestone](../../../wip/scheduling-and-threads.md),
recorded on 2026-10-05 before any SMP code change. Later tasks repeat the matched
workloads below and compare against these numbers.

These are nested-VM measurements. The development host is itself a KVM guest
(Fedora 44, Linux 6.19.10-300.fc44, 8 vCPUs, 15 GiB RAM, virtualized i9-12900K),
so the results are not owner-host or physical-hardware numbers. All values are
elapsed wall time, not CPU time.

## Configuration

| Item | Value |
| --- | --- |
| Pyxis | main `83c08d6` |
| Pins | fs `d352c7e`, userspace `a3ea1b0`, ports `36d952e`, lwIP `a1aadb9` |
| Build | Pyxis GCC 16.2.0, `make -j16 image`, kernel `CONFIG_XHCI=y` (the default) |
| Kernel ELF / ISO SHA-256 | `d0d0fde7…3f406` / `fa2878bf…88563` |
| QEMU | 10.2.2 with upstream AHCI fix `d9f78431d8eb`, KVM, Q35, `-cpu max`, 256 MiB |
| CPUs | one boot with 4 CPUs, one with 1 CPU |
| Firmware | OVMF CODE/VARS from `/usr/share/edk2/ovmf`, fresh VARS copy per boot |
| Devices | virtio-blk (native pool), virtio-fs, virtio-net (user), virtio-rng, `qemu-xhci` with one USB mass-storage stick |
| Backing | Every disk image, the virtio-fs export and the ISO copy were regular files under `/dev/shm` |
| Host tools | virtiofsd 1.14.0, classic ttcp 1.12 (Fedora `ttcp-1.12-50.fc41`) |

The build used the normal configuration plus a temporary, uncommitted init and
explicit session selections:

```sh
make -j16 image INIT=/path/to/init-baseline.sh INIT_PRIMARY=app://init INIT_CPUS= \
  MOUNT_DISK=12345678-1234-4567-89ab-0123456789ab
```

```text
#!app://shell.pxe
title --optional "Baseline"
mount --partition 1 --volume bench --read-write data://
mount --optional --read-write host
namespace create
service start text app://textfs.pxe
session app://session.pxe --configure-network --start-remote-services
```

With 4 CPUs this init and every workload ran on CPU 1. CPUs 2 and 3 ran idle init,
and the BSP ran only kernel work. With 1 CPU, everything ran on the BSP.
xHCI was enabled and active in both boots. It enumerated the stick (`46f4:0001`,
32768 × 512-byte blocks, writable, flush qualified), and its controller worker
stayed running. The stick was a blank 16 MiB image; it was neither mounted nor
written, and its block counters did not change during the workloads.

The native pool was 128 MiB with an 8 MiB journal and one `bench` volume holding
the 1 MiB `iobench.bin` fixture. It sat at sector 2049 of a 132 MiB GPT disk
attached with `cache=writeback`. The commands followed the
[npfs write-run record](../npfs-io-runs/README.md), using `mkfs.npfs --size 128MiB
--journal 8MiB --volume bench --source DIR`, `sgdisk` and `dd`. The virtio-fs
export held a copy of the same fixture. virtiofsd ran with the flags from
virtio-fs setup. The QEMU
command matched the npfs record, with these differences:

```text
-smp cpus=N,sockets=1,cores=N,threads=1 -m 256M
-machine q35,memory-backend=mem0 -object memory-backend-memfd,id=mem0,size=256M,share=on
-chardev socket,id=fs0,path=SOCKET -device vhost-user-fs-pci,chardev=fs0,tag=pyxis-host
-device qemu-xhci,id=xhci -device usb-storage,bus=xhci.0,port=1,drive=usbdisk
-netdev user,id=net0,hostfwd=tcp:127.0.0.1:23411-10.0.2.15:2323
-display none -serial file:SERIAL -monitor unix:MONITOR,server,nowait -gdb tcp:127.0.0.1:12411
```

Each guest command ran in a `build/tools/pyxis-remote --machine --no-shell-echo`
session. The next command was submitted only after the previous one reported its
typed completion. For a concurrent set, each client got its own remote session.
All sessions reported ready before their commands were submitted together.
Aggregate time runs on the host from the first submission to the last completion.
These sessions are children of the CPU-1 remote server, so today every client runs
on CPU 1. No debugger was attached during timed work, except for the snapshot
described below. Profiling was off except in the two `--profile` rows.

The remote session cannot run `iobench pipe` or `ipcbench`, because those need the
`session` handoff ([remote terminal limits](../../../userland/remote-terminal.md)).
Driving them would mean scripting the local framebuffer console, so they are not
part of this baseline. The pipe numbers in the
BSP request record
remain the only historical context for them.

## Results

Every workload below exited 0 and verified all of its samples. There were no
allocation failures, short transfers or failed passes. Values are the median, with
the minimum–maximum range in parentheses.

### Allocation

Each row is five fresh `allocbench` processes, one measured interval each, in ms.

| Workload | 4 CPUs | 1 CPU |
| --- | ---: | ---: |
| `allocbench heap` | 12.965 (12.712–13.140) | 14.635 (13.284–14.721) |
| `allocbench heap --mixed --size 4096 --live 256` | 49.116 (48.614–49.347) | 56.020 (55.536–57.462) |
| `allocbench growth` (128 × 64 KiB backing) | 48.503 (47.862–66.273) | 45.891 (45.253–62.896) |
| `allocbench pages` (64 × 64 KiB allocate/release) | 35.659 (34.883–36.445) | 35.493 (34.584–36.241) |

One profiled run of each backing workload splits the BSP request time into queue
time and service time (totals, in ms):

| Profiled run | CPUs | Elapsed | Allocate queue / service | Release queue / service |
| --- | ---: | ---: | ---: | ---: |
| `growth --profile` | 4 | 66.716 | 20.538 / 26.125 | none |
| `growth --profile` | 1 | 70.217 | 15.110 / 23.102 | none |
| `pages --profile` | 4 | 55.113 | 9.835 / 10.830 | 11.496 / 3.659 |
| `pages --profile` | 1 | 59.418 | 8.840 / 9.510 | 7.503 / 3.635 |

### Files

Each row is one `iobench` invocation of 1 MiB: one verified warmup plus five
measured samples, in ms. The default request size was used (4088 bytes for read,
4080 for write/copy). Writes grew from zero unless the row says prepared.

| Interval | 4 CPUs | 1 CPU |
| --- | ---: | ---: |
| Archive read payload (`app://`) | 0.260 (0.253–0.330) | 0.253 (0.249–0.258) |
| RAM read payload (`home://`) | 0.262 (0.249–0.322) | 0.248 (0.248–0.252) |
| RAM grow-write transfer | 8.517 (5.794–9.705) | 7.124 (6.468–9.385) |
| RAM copy transfer | 7.598 (6.223–9.852) | 6.956 (5.960–9.968) |
| HOST read OPEN | 2.632 (1.441–3.559) | 5.180 (3.370–5.892) |
| HOST read payload | 155.900 (146.593–160.966) | 168.345 (158.454–171.067) |
| HOST grow-write transfer / sync | 151.782 (146.885–153.982) / 0.536 | 164.711 (157.439–166.800) / 0.601 |
| HOST copy transfer / sync | 151.964 (144.567–154.590) / 0.522 | 163.995 (158.548–167.205) / 0.607 |
| Native read OPEN | 0.765 (0.729–1.059) | 1.565 (1.519–2.969) |
| Native read payload | 155.305 (145.192–164.626) | 189.740 (179.852–192.628) |
| Native grow-write transfer | 194.138 (189.695–208.261) | 224.732 (224.238–226.520) |
| Native grow-write sync | 35.913 (29.114–36.584) | 36.218 (35.774–36.750) |
| Native prepared-write transfer | 136.747 (103.392–152.478) | 182.158 (181.326–182.557) |
| Native prepared-write sync | 25.943 (25.529–26.998) | 26.535 (25.964–26.987) |
| Native copy transfer | 191.900 (188.630–199.287) | 224.926 (224.355–225.957) |
| Native copy sync | 36.171 (36.023–36.451) | 36.439 (35.888–36.822) |

Native commands were `iobench write data://grow.bin --sync`,
`iobench write data://prepared.bin --prepared --sync`,
`iobench copy app://share/iobench.bin data://copy.bin --sync` and `sync data://`.
The host and RAM commands were the equivalents from the
I/O reproduction list.
QEMU `info blockstats` snapshots bracketed each complete native write command,
including warmup, preparation and checkpoint work. The table shows the deltas for
the native pool disk:

| Command window | 4 CPUs: bytes / writes / flushes | 1 CPU: bytes / writes / flushes |
| --- | ---: | ---: |
| Grow write (6 MiB logical) | 7,835,648 / 400 / 176 | 7,794,688 / 393 / 172 |
| Prepared write (12 MiB including preparation) | 13,316,096 / 356 / 100 | 13,348,864 / 363 / 104 |
| Copy plus `sync data://` | 7,786,496 / 392 / 172 | 7,786,496 / 392 / 172 |

### TCP

Five runs of `ttcp -t 10.0.2.2` each sent 2048 × 8192 bytes to a fresh host
`ttcp -r -s -p 5001`. Guest throughput, including closure:

| CPUs | Median (range), MiB/s |
| --- | ---: |
| 4 | 1.334 (1.331–1.335) |
| 1 | 1.266 (1.264–1.268) |

In the 4-CPU runs, each host receiver confirmed 16,777,216 bytes. The 1-CPU
receiver output was discarded, so only the guest's own byte reports remain for
those runs.

### Concurrent sessions

Each set ran three times. Every repetition first ran single-client controls with
the same arguments, one after another. `H` is `allocbench heap --rounds 262144`,
which is pure userspace computation. `P` is `allocbench pages --rounds 2048`,
which exercises BSP private-memory requests. The mixed set ran H, P,
`iobench write data://mixN.bin --rounds 2 --sync` and `ttcp -t -n 192 10.0.2.2` together.

| Set | 4 CPUs | 1 CPU |
| --- | ---: | ---: |
| H alone, internal elapsed | 0.792 s (0.791–0.793) | 0.863 s (0.862–0.865) |
| 2 × H aggregate | 1.700 s (1.699–1.708) | 1.856 s (1.853–1.859) |
| 4 × H aggregate | 3.345 s (3.330–3.348) | 3.563 s (3.553–3.573) |
| P alone, internal elapsed | 1.013 s (0.954–1.015) | 0.954 s (0.952–0.955) |
| 2 × P aggregate | 0.945 s (0.937–0.948) | 2.090 s (2.085–2.091) |
| 4 × P aggregate | 1.947 s (1.937–1.949) | 4.243 s (4.239–4.249) |
| Mixed aggregate | 3.466 s (3.415–3.468) | 4.692 s (4.688–4.696) |

The table below shows each client in the mixed set against its own single-client
control (ranges over three repetitions):

| Client | 4 CPUs alone | 4 CPUs mixed | 1 CPU alone | 1 CPU mixed |
| --- | ---: | ---: | ---: | ---: |
| H elapsed | 791–793 ms | 840–904 ms | 862–865 ms | 1018–1029 ms |
| P elapsed | 954–1015 ms | 3309–3338 ms | 952–955 ms | 4545–4577 ms |
| Native write transfer (median of 2) | 182–194 ms | 520–537 ms | 224–226 ms | 601–618 ms |
| Native write sync (median of 2) | 29–37 ms | 68–90 ms | 36–37 ms | 83–100 ms |
| ttcp 1.5 MiB | 1.334–1.474 MiB/s | 0.563–0.576 MiB/s | 1.244–1.270 MiB/s | 0.386–0.398 MiB/s |

### Placement snapshot

During four concurrent `allocbench heap --rounds 524288` clients on the 4-CPU boot,
a read-only GDB batch did the following (the commands are not kept
in the tree):

- It found CPU 1 running a user task (cpu_index 1) under its private CR3, with
  three more tasks in CPU 1's ready queue.
- It found CPUs 0, 2 and 3 halted in the scheduler idle loop under the kernel
  root, with empty ready queues.

The four clients took 6.533 s in aggregate, including the stop.

### Kernel heap growth

This is context for task-1 decision 4, the heap arena, now implemented.
Three further boots used the same image and configuration, each with a freshly
made pool disk. Their timings were not used, because GDB attached between groups.

The first two boots, at 4 and 1 CPUs, repeated the full workload sequence above
in the same order. A GDB batch attach read the kernel heap counters
(`'kernel/mm/heap.c'::stats`) after boot and after each workload group.

| Point | Pools | Pool bytes | Live allocations (4 / 1 CPU) | Live block bytes (4 / 1 CPU) |
| --- | ---: | ---: | ---: | ---: |
| After boot | 9 | 3,624,960 | 2,003 / 1,978 | 2,756,408 / 2,733,216 |
| After allocation set | 9 | 3,624,960 | 2,006 / 1,978 | 2,756,880 / 2,733,216 |
| After RAM I/O | 15 | 12,255,232 | 2,015 / 1,988 | 9,024,216 / 9,000,600 |
| After HOST, native, TCP and concurrent sets | 15 | 12,255,232 | 2,055 / 2,018 | 9,033,984 / 9,008,192 |

Pool counts and bytes were identical at both CPU counts at every point, so the
two counts share one column. In this sequence, the kernel heap grew only during
the RAM-file workloads.

`allocbench` does not use `kmalloc`: its private memory comes from the PMM and VM
directly, and so does the native filesystem's cache and buffers. HOST, TCP and
the concurrent sets fit in existing pools.

The third boot (4 CPUs) placed a GDB `dprintf` after pool publication in
`add_pool()` and ran the RAM commands one at a time:

| Command | Pools added | Sizes |
| --- | ---: | --- |
| `cat app://share/iobench.bin > home://iobench.bin` | 4 | 282,624; 561,152; 1,114,112; 2,224,128 |
| `iobench write home://grow.bin` (warmup + 5 × 1 MiB) | 1 | 2,224,128 |
| `iobench copy app://share/iobench.bin home://copy.bin` | 1 | 2,224,128 |

RAM FILE backing is a `kmalloc` buffer whose capacity doubles as the file grows,
so a growing RAM file adds pools until its buffers fit. Each pool's size follows
the request, not the 256 KiB minimum. Growth was rare: 6 events across the 100 workload
commands, all from the three RAM-file commands. Each event mapped and zeroed between 69 and 543
pages. No growth duration was measured, because a timing breakpoint would itself
distort the result.

## Observations

- With 4 CPUs, concurrent compute-bound sessions do not scale: 2 × H and 4 × H
  take about 2.1 and 4.2 times one client. The snapshot shows why: today all of
  them share CPU 1, while three CPUs are idle.
- With 4 CPUs, two P clients finished in less time than one P client alone.
  4 × P took about twice a single client. A likely cause, not measured here, is
  that each private-memory call parks the caller while the BSP services it, so a
  second client overlaps with that round trip and keeps CPU 1 out of idle HLT.
  Idle wake cost is a known nested-VM confounder. With 1 CPU, P scales linearly
  with the client count.
- Mixed load slows every BSP-serviced client. Measured against their own
  controls, P took about 3.3 times longer, the native write transfer about 2.8
  times, native sync about 2.2 times, and TCP throughput fell to about 0.42 of its
  single-client rate. CPU-bound H changed by about 10%. These clients share
  CPU 1 and also compete for the BSP executor and the BSP workers (filesystem,
  network, xHCI), so this experiment cannot separate the two causes. Later tasks
  should repeat the identical set, so that the change from spreading user work
  can be seen separately from the BSP services that remain serial.
- 1-CPU is slower than 4-CPU for HOST, native and TCP. Here the BSP also runs
  userspace, the presenter and the xHCI worker. The difference is a property of
  this baseline, not a target.
- Allocation numbers differ from older records such as the
  [allocation profiling](../../allocation-profiling.md) growth of 36.983 ms.
  Revision, device set and active workers all differ, so those records are not a
  matched comparison with this one.

## Integrity and cleanup

After each boot, `sync data://` completed and QEMU quit through the monitor. Then:

- The extracted pool passed `fsck.npfs`.
- `npfs-inspect extract` of every native output (grow, prepared, copy, single and
  mixed files) matched the installed fixture byte for byte.
- The host-side virtio-fs outputs matched as well.

Every remote client ended with `drain: complete`. The QEMU, virtiofsd and ttcp
processes were stopped, and the `/dev/shm` images were removed after the record
was assembled.
