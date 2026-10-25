# Shared SHA-256 qualification

## Revisions and workload

2026-10-25: before userland `8a2ff95e`, ports `05ab881f`; after userland
`b468f20c`, ports `6520b079`, core `f5ff8bbd`. Same SDK C runtime and Clang23
container `pyxis-llvm23.1.3-3d69e83`; no kernel/Go change or ABI rewrite.
Four-vCPU nested KVM QEMU, `max,+invtsc`, 4 GiB, modern VirtIO networking/SCSI,
unchanged kernel `e57d9701e051` (ABI 0.0.8), RAM FILE/packages. These are
nested-VM observations with invariant TSC, not native throughput.

Private helpers fill 64 MiB with `byte(i*131+(i>>8)*17+23)`, then measure
memory hash, FILE byte scan and FILE read-and-hash three times each. Timed
target monotonic intervals exclude preparation, allocation and printing;
FILE reads and hash caller updates are 16 KiB. Before uses the existing PSA
wrapper (internally 4 KiB copying updates); after uses libsha256. Full expected
SHA-256: `26e22ffbacc0938c9dc32c331ab3331a6acab06cc056e1e0034c6511ff7527e4`.

## Interleaved A1/B1/A2/B2

Six samples per variant; all counts and complete digests matched:

| MiB/s, median (range) | Before | Shared SHA-NI |
| --- | --- | --- |
| Memory hash | 376.5 (297.3–381.9) | 2186.3 (2172.4–2200.5) |
| RAM FILE scan | 1937.2 (1664.3–2064.4) | 2028.9 (1919.3–2093.3) |
| RAM FILE read + hash | 331.2 (296.8–335.2) | 1193.6 (1179.5–1203.4) |

Each sample also installed a distinct `sha-payload` version containing the
same public 268 MiB/1242-file toolchain payload, then verified the whole selected
set. All four fresh extractions and the final verification passed. Distinct
versions prevent immutable-store reuse. These are unsigned byte fixtures with
normal ABI checks, not usable compiler installations.

Package phase spans are host Remote arrival observations, including buffering,
scheduling and RTT. In A1/B1/A2/B2 order: archive verify 12.76/11.80/10.93/10.72 s;
extract 16.06/15.78/15.75/15.71 s; complete `pkg verify` envelopes
3.02/2.25/2.92/2.32 s. Fetch was 16.67/10.25/10.18/10.00 s, so full-install
speedup must not be attributed solely to hashing. Forwarded HTTP FILE reads
remain bounded to 4088 bytes per RPC; memory-hash gains do not establish equal
end-to-end package gains. Provider/queue/durability behavior was not changed.

## Correctness and fallback

Private host checks: 609 backend cases per GCC16 and Clang23; 182 complete
streaming combinations; 274 independent portable SHA-224/SHA-256 comparisons;
484 raw Mbed/PSA SHA-224/SHA-256 comparisons. Invalid-buffer, length bound,
finalization/reset and unaligned/boundary inputs passed. Target backend and
dispatch disassembly contains no VEX/EVEX/AVX or YMM/ZMM use.

Fresh QEMU `max,-sha-ni,+invtsc` reported CPUID SHA=0, SSSE3/SSE4.1=1,
OSXSAVE=0. Portable library, Lua, sha256sum, new pkg install/verify and PSA
compression bridge passed with full digests. This tests feature dispatch in KVM,
not execution on an older physical processor. New host Remote + new xfer
uploaded/downloaded 65,537 bytes with explicit confirmations and equal bytes.
Host GDB exact-ELF digest checking and invalid/missing-ELF refusal also passed.

## Native follow-up

Prepared only: `/shared/present/sha256/vela-check/` has the matched artifacts,
notices, hashes and hands-free collector. The orchestrator schedules the existing
HWP live PXE environment on vela; no native staging or execution occurred here.
It repeats A/B/A/B with RAM packages/home/tmp, target memory/FILE timings and
Remote-observed verify/extract phases. It refuses reused fixture identities or
conflicting repository configuration; an unconfirmed command is never retried.
Raw evidence remains under `/shared/present/sha256/qualification/` and
`vela-check/results-pbt657zo/`. No benchmark framework or instrumentation is merged.
