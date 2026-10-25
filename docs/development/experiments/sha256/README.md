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
speedup must not be attributed solely to hashing. FILE reads remain bounded to 4088 data bytes. RAM-backed HTTP snapshots have
a synchronous kernel read path that bypasses the userspace receiver; these
measurements do not establish a userspace-provider round trip for each read.
Memory-hash gains do not establish equal end-to-end package gains. Provider/
queue/durability behavior was not changed.

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

## Native vela A/B/A/B — passed

Orchestrator-run, 2026-10-25: vela on the existing HWP live PXE image,
`82ae36202798` archive plus `caelum-hwp.elf`, ABI 0.0.8, RAM packages/home/tmp.
The same before/after artifacts above ran A1/B1/A2/B2 through reverse Remote.
All four helper counts/digests, fresh 1242-file extractions, selected-set checks
and the final verification passed. No NVMe backing or persistent write was used.

Six target-timed samples per variant (three repetitions in each of two runs):

| MiB/s, median (range) | Before PSA | Shared SHA-NI |
| --- | --- | --- |
| Memory hash, 4 KiB updates | 426.0 (424.1–431.7) | 3655.9 (3613.9–3680.3) |
| Memory hash, 16 KiB updates | 424.0 (419.8–430.1) | 3661.8 (3612.2–3706.1) |
| RAM FILE scan | 1697.5 (1670.0–1726.2) | 1706.7 (1684.4–1716.4) |
| RAM FILE read + hash | 359.5 (357.2–362.3) | 1385.6 (1360.3–1397.4) |

The 4 KiB memory median improves 8.6 times and read-plus-hash 3.9 times;
FILE scanning is effectively unchanged. These are pooled sample medians;
individual-run medians and rounded relay summaries differ slightly.

For the identical 281,031,168-byte archive/1242-file fixture, phase spans below
are **host Remote arrivals**, including transport/scheduling, not target-internal
phase timers. VERIFY spans first progress through 100%; that final report occurs
before digest finalization/comparison. EXTRACT spans its own progress lines.

| Seconds | A1 old | B1 new | A2 old | B2 new |
| --- | --- | --- | --- | --- |
| Fetch | 5.65 | 5.66 | 5.94 | 5.66 |
| Archive VERIFY | 3.23 | 2.67 | 3.20 | 2.63 |
| EXTRACT | 4.72 | 4.15 | 4.73 | 4.15 |
| Separate `pkg verify` command envelope | 1.76 | 1.21 | 1.78 | 1.22 |

The archive phase is about 100 MiB/s after acceleration, below the helper's
1386 MiB/s RAM read-and-hash. This is not the separate `pkg verify` command,
which checks the installed files. The capture alone does not divide remaining
cost among syscall/transport validation, copying and progress observation.
No further optimization or removal of validation/durability barriers is implied.

Packet, exact commands, artifact hashes and raw native evidence:
`/shared/present/sha256/vela-check/`, especially `results-zsaf0xyn/results.json`.
Nested evidence remains under `/shared/present/sha256/qualification/` and
`vela-check/results-pbt657zo/`. No benchmark framework or instrumentation is merged.
