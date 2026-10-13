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
SDK before repeated 1/4-CPU nested-KVM timing runs. No probe runs during timing.

Initial old task 4 binaries were rejected as stale setup: current directory
messages are 72 bytes, while their old SDK/embedded LLVM Support sent 64 bytes.
Relinking libc/libpyxis alone is insufficient because native LLVM Path also
embeds that structure. The unchanged source is rebuilt against current headers;
these setup failures are not a compiler performance regression or measured baseline.

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
This is a **probe build**, not a normal PXE kernel; no runtime counters have yet
been qualified or observed. Record each actual probe image/hash before use.

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
