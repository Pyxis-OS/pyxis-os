# Hosted Clang task 5

Status: baseline/qualification in progress, 2026-10-13. No native task 5
performance or peak measurements yet; the earlier "instant" compile/extract
observation is qualitative. Task 5 uses the accepted separate npfs toolchain
volume and no-launcher bundles; owner/luna handle native staging and runs.

## Before implementation

Untouched baseline base `0e95af3a82afc2ea8f009c046fa96cb6c08ba1bc`; userland
`c472de7d`, ports `f93becb9`, fs `414be0ac`, lwIP `a1aadb91`. Published compiler
image/source `pyxis-llvm23.1.3-4c23a20` / `4c23a208`. Ordinary `make -j16 image`
passes. Fresh native Clang/LLD builds use unchanged fork source and the current
SDK. The baseline completed before task 5 implementation and probe runs.

Initial old task 4 binaries were rejected as stale setup: current directory
messages are 72 bytes, while their old SDK/embedded LLVM Support sent 64 bytes.
Relinking libc/libpyxis alone is insufficient because native LLVM Path also
embeds that structure. The unchanged source is rebuilt against current headers;
these setup failures are not a compiler performance regression or measured baseline.

QEMU 10.2.2, Q35, nested KVM/host CPU, 2 GiB, fresh OVMF/UTC, 1 then 4 CPUs,
VGA, VirtIO SCSI/net/RNG and private read-only virtio-fs bundles. Three serial
samples per workload; no debugger/probe during timing, host load unisolated.
`/usr/bin/time` measures the existing `pyxis-remote --machine --no-shell-echo`
session through FINAL/drain, including transport, launch and cleanup. These
are not internal compiler CPU times or physical-host measurements.

| End-to-end seconds, median (range) | 1 CPU | 4 CPUs |
| --- | --- | --- |
| Clang version/startup | 2.33 (1.93–2.56) | 1.99 (1.98–2.36) |
| Hello compile | 2.11 (2.09–2.11) | 2.15 (2.14–2.20) |
| Rejected input | 2.03 (2.03–2.03) | 2.09 (2.05–2.10) |
| Standalone LLD | 1.94 (1.93–1.94) | 1.82 (1.77–1.93) |
| Empty session | 0.02 (0.02–0.02) | 0.02 (0.01–0.02) |

Version/compile/link exit 0; the baseline's undeclared-identifier input exits 1
without an object. The linked PXE prints `hello 5`, exit 0. Both CPU counts
produce identical 1,160-byte ELF objects and identical PXEs. All sessions drain
completely. Exact configuration, commands, hashes and raw samples:
`/shared/present/batch2/hosted-clang-task5/baseline/`.

| Fresh baseline artifact | Serialized bytes | Page-rounded image/span bytes |
| --- | ---: | ---: |
| Clang | 90,335,786 | 90,927,104 |
| LLD | 55,731,426 | 56,340,480 |

Each manifest requests an 8,388,608-byte guarded stack, read-only resource/sysroot
roots and the accepted four runtime grants, without a launcher. All three
PT_LOAD segments match the separately linked symbol ELF byte-for-byte. Both
existing loader ceilings are satisfied; these sizes do not bound working memory.

## Authorized measurement-only probe

Separate local branch `probe/hosted-vm-peak`, base `0e95af3a82afc2ea8f009c046fa96cb6c08ba1bc`,
commit `2b9f1d94ce71233d3472ecade1bcffb8cc529da3`. Never merge/cherry-pick this
probe into shipping branches. Reproducible patch:

- Path: `/shared/present/batch2/hosted-clang-task5/probe/owned-vm-peak.patch`
- SHA-256: `83de457aea3668a799571d66a13ee97cf5ba45b3d5c54c9bd8f80de1b22fa6dd`
- Exact build/metric notes: `/shared/present/batch2/hosted-clang-task5/probe/record.txt`

Apply that patch to its exact base, initialize pinned fs/lwIP, and build with the
published compiler using `make -j16 kernel LOG_LEVEL=trace`. Ordinary kernel build
passes. Initial probe ELF SHA-256:
`2dfa41fb1de31d0f056224954d49c23e7c12928cbdd2bb9c73fe08626d2d4274`.
This is a **probe build**, not a normal PXE kernel. Record each actual probe
image/hash before use.

Metric: exact event-based lifetime peak owned physical data backing of one
private VM, including failed provisional backing. Image/BSS/padding, eager
initial stack, startup pages and private allocations (including direct LLVM
regions) count. Guards/reservations, page tables, kernel metadata, filesystem/
cache, device/shared/borrowed mappings and kernel stacks do not. Threads share
one VM; children have independent IDs, so their peaks do not add to a tree peak.
Explicit capture-range tags measure separate per-boot simultaneous selected-image
payload backing, including rollback, without charging nested kernel allocations.

Startup/final-retirement summaries and capture allocation/release records use
`ktrace` only. VM IDs identify records; final cleanup should report zero owned
backing. Profiled measurements remain separate from untouched-main timings;
these counters are not malloc payload usage, sampled RSS or total attributable
physical footprint. No public profiling ABI, quota or compiler-container rebuild.
