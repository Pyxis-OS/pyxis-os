# Caller-CPU preparation and RAM retirement

Task 4 of [BSP removal](../../wip/bsp-removal.md), accepted placement/lifetime
rules in #1550. This record separates source inspection, nested QEMU results
and pending native qualification. No new guest benchmark or profiling code is
part of the implementation.

## Pre-code baseline

Captured before kernel edits at clean main `af35537d69f2`, using builder
`pyxis-llvm23.1.3-8bdf136` (Clang 23.1.3), `LOG_LEVEL=info`, ordinary `-O2`
kernel flags and the same effective Kconfig. Dependency pins: userland
`b714efef7652`, ports `f24ee205ca55`, fs `f3f22b6e07e7`, lwIP `4fb74e1b9213`.
Current matching finished SDK/userland/ports artifacts were verified and reused;
no compiler container was rebuilt. The independent existing Go qualifier retains
its original provenance (`01cf32642ff9`), rather than claiming a fresh Go build.

Q35/nested KVM, host CPU with invariant TSC, one or four vCPUs, 4 GiB, fresh OVMF
variables per boot, modern VirtIO network/RNG/SCSI, RAM-backed home/state. Boot:
`init=boot://boot-init.pxe nvme.write=0 display.timing=off log.level=info`.
Development/network setup is configured on CPU0; Remote allows CPU0 for the
one-CPU boot or CPUs 1/3 for four CPUs. One complete warmup and three retained
fresh boots for each configuration; two independent concurrent Remote sessions
repeat launches/pages in four-CPU boots. The timings do not prove those baseline
launchers actually occupied distinct CPUs. A separate untimed inspection of the
implementation establishes actor placement; it is not timing evidence.

Commands use existing tools:

```text
session boot://lua.pxb -e "for i=1,16 do assert(pyxis.run{'echo','-n'}==0) end; print('launches=16')"
session boot://lua.pxb -e "for i=1,1024 do assert(pyxis.run{'echo','-n'}==0) end; print('launches=1024')"
boot://allocbench.pxe pages --size 65536 --live 8 --rounds 100
session boot://ipcbench.pxe call --size 64 --messages 256 --rounds 100
session boot://ipcbench.pxe call --size 4096 --messages 256 --rounds 100
session boot://ipcbench.pxe send --size 64 --messages 256 --rounds 100
session boot://ipcbench.pxe send --size 4096 --messages 256 --rounds 100
session boot://threadprobe.pxe lifecycle
session boot://threadprobe.pxe tls
boot://cpu-go.pxb
boot://nvim.pxb --headless -u NONE -i NONE "+qall!"
```

Every launch waits for real child completion; each command additionally checks
Remote FINAL with exited/0 and complete drainage. Counts, page releases, IPC
verification and Go's 32-worker total 5,280,000 passed. Neovim supplies the
available larger-image launch/exit control. A failed initial warmup parser and
an unsupported second named Remote listener were preserved and excluded before
retained samples; they do not change the matched workload.

Median (range), milliseconds across three fresh boots:

| Workload | 1 CPU | 4 CPUs |
| --- | --- | --- |
| 1,024 launches, client wall through FINAL | 1152.413 (957.651–1223.291) | 1051.447 (1003.820–1163.890) |
| Private pages, guest elapsed | 19.191 (18.471–28.061) | 18.238 (17.511–20.446) |
| CALL64, summed guest completion intervals | 133.587 (127.553–153.591) | 147.005 (145.133–158.784) |
| CALL4096, summed guest completion intervals | 153.308 (150.197–161.357) | 161.104 (151.555–241.533) |
| Thread lifecycle, client wall | 58.267 (46.275–63.888) | 34.307 (30.586–39.852) |
| TLS lifecycle, client wall | 89.515 (75.446–90.942) | 74.440 (72.597–74.574) |

Concurrent four-CPU launch median 1758.123 (1746.826–2281.867) ms, and pages
24.527 (23.275–28.341) ms, across six worker observations. These are shared-host
nested results: host load was about 9 on 12 CPUs, with unrelated agents' work;
no own build or debugger overlapped a retained timing. End-to-end client wall
includes connection/transport/cleanup and is not phase attribution. IPC intervals
exclude setup/retirement and retain their tool's own warmup. Clock cost is not
subtracted. Native speedup is not established by these numbers.

Small baseline evidence and full hash/command manifest:
`/shared/present/bsp-removal/task4/baseline/`. Kernel SHA-256:
`db23ee2d81f98d864ca8c990067c7d45705e5b5bb52af2f1f49880bbe9d45390`.
Builds and frozen images remain VM-local.

## Implementation qualification

The ordinary four-CPU cohort passes after integration, including two independent
concurrent launchers. A first boot exposed missing shared-owner admission in the
split fixed space phase; a subsequent concurrent run exposed try-only admission
for the small task-stack reservation. Both were corrected before matched after
measurement. Source review corrected departure-CPU accounting independently of
BSP queue order and preserved group attribution around early capture unwind.
These debugging runs are functional evidence, not retained timing samples.

Matched after measurements and hands-free vela qualification are pending. The
fresh baseline remains frozen; no native action or PXE staging has occurred.

## Storage and remaining limits

Artifact type inspection: task 840 bytes, process 432 bytes (baseline 392),
request area 6,592 bytes (baseline 6,560), profile 776 bytes. Each mandatory fixed
RAM actor adds a 16 KiB stack and 840-byte task record per online CPU. Queue/owner
arrays add 88 bytes per CPU: RAM-process owner 48, launch queue 16, captured-range
queue 16 and task-queue head 8. That is 17,312 bytes per CPU excluding allocation
headers/page tables; no free-frame cache is introduced. These sizes are build
observations, not ABI contracts.

Device callbacks, endpoint weak lists and actual object destructors retain their
fixed owners. Shared kernel range admission can still serialize the small stack
and captured-range portions. Process/thread completion follows actual RAM work;
groups additionally join attributed deferred device/object cleanup. A failed
shared capture acknowledgement retains VA, backing and group cleanup until
reboot, with no late-ACK retry; intentional timeout injection is not claimed as
qualified by ordinary runs. Task 5's remaining consumer migration is separate.
