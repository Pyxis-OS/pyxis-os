# Launcher capacity comparison (2026-10-30)

WP3 removes the fixed capture/startup, serialized-image, image-span and batch
ceilings, while retaining eager image/stack backing and atomic publication.
The implemented contract is in [program loading](../../../kernel/program-loading.md).
This comparison measures ordinary small workloads, not native hardware or the
maximum workload a machine can admit.

## Matched inputs

Baseline parent `d0c8248ca5adedde6db055ef9618f9911b989fae`, userland
`41b493846555a94bf120daf88730604f6f868ba3`, ports
`7695ecbacf868b5891bfb90a2d43f5cc1a0a919c`. Candidate parent
`d97fb3907b1b55c0b731ec171ed26284d1204d22`, userland
`15aabe1312c22e6da92f701ef35c5580947ba71a`, ports
`7828d8b247e6012e1e350f7403f38b1d9487103d`. The candidate contains WP3 on the
same baseline, before the later package-index rebase. Subsequent documentation
and integration changes are not claimed as new timing measurements.

Both images use all native userland and exactly eleven support recipes: Lua,
picohttpparser, mbedtls, zlib, libpng, wpa-supplicant, BusyBox, ca-certificates,
tzdata, pciids and usbids. File/type inventories match exactly (786 initrd
files); packages.lua and limine.conf are identical. No capacity fixtures or
extra games are present. The necessary source/ABI changes increase the initrd
from 40,333,312 to 40,622,592 bytes. Lua's serialized executable changes from
587,710 to 592,021 bytes.

Both fresh guests use nested KVM, Q35, CPU max, four vCPUs, 4 GiB RAM, OVMF,
VirtIO block/network/RNG and the same blank 128 MiB disk. Effective kernel
configuration SHA256 is
`5bf769cb7ee37bd3b2bb57430e0ef663f4600f8184f628389a08b7951c30136a`.
All task-owned builds were stopped. After 16 warm-up commands, the existing
remote client ran three sessions per workload, waiting for complete output
drain. Times are host wall time, including session/transport overhead; they
do not isolate kernel CPU time. Temporary measurement clients are outside Git.

## Results

Seconds: median (minimum–maximum), three repeats per image.

| Workload | Baseline | WP3 |
| --- | --- | --- |
| 1,024 Lua `pyxis.run` launches of `echo -n` | 0.911 (0.861–0.913) | 0.851 (0.839–0.868) |
| 128 two-stage shell pipelines | 0.444 (0.440–0.445) | 0.483 (0.468–0.490) |
| 128 eight-stage shell pipelines | 1.444 (1.420–1.455) | 1.543 (1.537–1.554) |
| 64 launches of a selected home FILE containing Lua | 0.168 (0.168–0.170) | 0.170 (0.169–0.171) |

Pipeline medians increase by 8.8% and 6.9%; the observed difference includes
request-sized ownership and scalable readiness-set setup. This experiment does
not attribute costs to individual functions. The small-launch distributions
overlap; the lower candidate median does not establish a speedup. Selected-file
capture differs by about 1% alongside the changed executable size and session
overhead, without evidence here that streaming would justify another ownership
contract.

A post-workload process snapshot reports the remote shell at 1,624/1,632 KiB
and remote-terminal at 1,352/1,356 KiB (baseline/candidate). These are process
accounting samples, not a proof of total session memory or absence of leaks.
Both guests powered off normally with exit status zero. The independent
capacity/refusal qualification and final composition checks are reported in the
WP3 PR; this timing comparison does not qualify native hardware.
