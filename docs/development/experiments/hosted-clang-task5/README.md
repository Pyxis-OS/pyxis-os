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

### Probe qualification

Separate labelled ISO SHA-256
`6c08e140fd925bbdae4791bdf78b93323574158d63b460da791c62279a92a8d6`,
with the exact probe kernel above and the freshly rebuilt baseline bundles.
Q35/nested KVM, 1/4 CPUs, 2 GiB, private read-only virtio-fs. Full trace-build
consistency checked; no instrumented timing claims.

| Workload | Startup owned bytes | Lifetime peak bytes | After cleanup |
| --- | ---: | ---: | ---: |
| Clang hello compile | 99,323,904 | 101,838,848 | 0 |
| Standalone LLD link | 64,737,280 | 70,078,464 | 0 |
| Clang undeclared identifier | 99,323,904 | 100,442,112 | 0 |
| Clang denied output | 99,323,904 | 99,983,360 | 0 |

Identical observations at both CPU counts. Compile/link/run exit 0 and retain
baseline object/PXE hashes; rejected input and denied output exit 1 without
output. Startup is image backing + 8 MiB stack + 8 KiB argument/startup pages.
Separate captures use 90,337,280 / 55,734,272 bytes, returning to zero after each
release. Their per-boot peak is 90,337,280 bytes; it must not be added to the
process peak as a simultaneous global peak.

Read-only GDB independently sums backed VM ranges at startup and retirement,
checks counter zero after cleanup, observes a real 64 KiB private allocation,
and verifies the existing allocbench's 4 KiB allocate/release changes
312 → 313 → 312 pages with peak 313 retained. On CPU 2 in the four-CPU boot,
Clang has one task, one active root, zero borrowers and live FS backing.

Fresh one-CPU 256 MiB boots reach a usable shell: both captures complete, then
image backing refuses allocation; provisional process peaks are 3,080,192 /
37,617,664 bytes and both clean up to zero. At 224 MiB, Clang capture itself
returns NO_MEMORY, current zero, provisional capture peak 59,944,960 bytes;
LLD captures successfully, then refuses image backing and cleans up to zero.
Subsequent echo/ls and complete session drain succeed, without panic. These
are real low-memory launch failures, not started-tool malloc failures or an
injected allocator hook. At 192 MiB Limine cannot load the full 83 MiB initrd;
that loader failure is excluded from kernel qualification.

Exact probe configuration, GDB observations, raw logs and native field
interpretation are in `probe-qualification.txt` beside the patch. All owned
probe QEMU/GDB/client/virtio-fs jobs are stopped. Native measurements and the
production pool payload's qualification remain pending.

## Fresh production payload

Opt-in producer commit `e8eadc28` (integrated as `26b67bc6`) completes from fresh
host generators and target objects with the existing published compiler. No
fork, container, runtime, ABI or ordinary image-build change. Both P1Fs are
byte-identical to the current-SDK baseline above. Static script syntax and
diff checks pass; inventory verifies 4,323 regular files plus directories, with
per-file hashes, normalized USTAR ownership/modes/timestamps and no symlinks.

- Revision: `hosted-8bb435668f5d5c899faccd634fb549aefd001883b465ef15728b66b9959fd8b0`
- Archive: 206,694,400 bytes; SHA-256
  `f4a821d87212f10354372ec5c5176ccba22c2b75d20e72f7b8dd353d729e6429`
- Owner artifact directory:
  `/shared/present/batch2/hosted-clang-task5/payloads/hosted-8bb435668f5d5c899faccd634fb549aefd001883b465ef15728b66b9959fd8b0/`

The archive contains only `clang.pxb`, `lld.pxb` and `metadata`; sources and
qualification results are separate. The metadata records exact SDK/source,
producer-file hashes, installed tools, fresh generators, CMake configuration,
image/backing sizes and notices. Production scripts match the recorded hashes.
The unused `LLD_INCLUDE_TESTS` configure warning is harmless: effective
`LLVM_INCLUDE_TESTS=OFF` excludes those tests.

Usage and the explicit authority/publication contract:
[native hosted tools](../../hosted-tools.md).
