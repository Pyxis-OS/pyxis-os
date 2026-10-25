# Copied bulk FILE reads

Qualification on 2026-10-25. Kernel implementation `30bcd37e`, foundation
`45f43e98`; userland `492473b` (documentation-only follow-up `faadd97`). A is
userland `67aa622`'s pkg, B its explicit bulk-read replacement. Both binaries
use the same newly built SDK/runtime and shared SHA-256 engine. Httpfs is the
same bulk-capable provider in both runs; A continues using ordinary READ.

## Configuration and commands

Nested KVM: q35, `-cpu host,+invtsc`, four vCPUs, 8 GiB, modern VirtIO network,
SCSI CD and rng. Caelum selects the stable KVM clock on all four CPUs. Live
packages/http-work/system are a sparse RAM-npfs pool; the 64 MiB helper file is
native RAM. A local HTTP server supplies the unchanged SHA fixture:
281,031,168 bytes, 1242 synthetic files, four distinct package versions.
No native/PXE action, raw physical-device write or compiler rebuild was done.
Builder: `pyxis-llvm23.1.3-3d69e83`, Clang 23.1.3, baseline x86-64, `-O2`.

The private adaptation of the existing SHA collector runs:

```text
pkg-before repository add --unsigned bulk-fixture http://10.0.2.2:18080/
# A1: before/a1, B1: bulk/b1, A2: before/a2, B2: bulk/b2
hash-control all tmp://bulk-control.bin 64
pkg-variant --yes install bulk-fixture/sha-payload@VERSION
pkg-variant verify
```

Each version extracts fresh. All four runs passed the full archive hash,
manifest/inventory/member checks, 1242-file extraction, publication and selected
set verification; the final B verify also passed. No check was replaced by a
control-helper digest. An earlier unmeasured A correctness run is excluded.

## Results

VERIFY and EXTRACT below are **host Remote output-arrival spans**, from first
to last phase line. Transport/buffering and progress coalescing apply; these are
neither native hardware timings nor exact target CPU attribution. The separate
`pkg verify` column is the host command envelope. Fetch prints its target-clock
duration. No profiled run contributes a timing sample.

| Run | Fetch, target s | VERIFY span s | EXTRACT span s | `pkg verify` envelope s |
| --- | ---: | ---: | ---: | ---: |
| A1 | 9.0 | 10.277 | 15.254 | 2.213 |
| B1 | 9.8 | 0.620 | 4.034 | 0.711 |
| A2 | 10.8 | 11.410 | 17.540 | 2.307 |
| B2 | 17.0 | 0.919 | 4.683 | 0.967 |

The fetch path is unchanged; its observed variation is not attributed to bulk
reads. The unchanged target memory-hashing control had A/B medians 2246/2212
MiB/s (ranges 2018–2249 / 2036–2249). Native RAM scan ranges were 2092–2121 /
1704–2127 MiB/s; read-and-hash ranges were 190–1234 / 977–1237. This shared nested
host has variable scheduling; interleaving and controls do not establish native
speed or statistical significance.

## Request counts (separate profiled runs)

Private linker wrappers count successful FILE helper calls only within the
archive VERIFY and EXTRACT callbacks. These builds are not timing samples;
no instrumentation is merged. Both profiled installs passed all checks.

| Phase | Ordinary calls | Bulk calls | Bytes returned, either path |
| --- | ---: | ---: | ---: |
| Whole-archive VERIFY | 85,764 | 269 | 281,031,168 |
| EXTRACT reads | 88,412 | 4087 | 281,027,991 |

VERIFY B returned 268 full 1 MiB chunks plus a 12,800-byte tail. A returned at
most 4088 bytes per call. EXTRACT B included 234 full chunks and 3194 small
reads for headers, padding and short members; all returned bytes match A.
Npfs retains its existing at-most-16-block (64 KiB) backend run machinery,
including dirty-cache and physical-contiguity checks. Helper request counts
are measured; individual device/backend run counts are not captured here.

## Correctness and limits

Private native/transport probes passed 98 checks: RAM/initrd/RAM-npfs and
persisted VirtIO-npfs reads, 1 MiB counts, EOF/short reads, bad ranges/deadlines,
ordinary READ, completion collection after source close, short/invalid output
retry, single-use collection, budget refusal, exports/forwarders/CUT and late
reply/no writes after return. A separate lifetime probe, repeated with the final kernel implementation,
passed 80 checks for
slot saturation, cleanup, receiver closure, cancellation, retained canceled
receipts and wrong-reply refusal. A separate read-only GDB inspection observed
a 1,048,576-byte native request returning an owned buffer of that capacity,
with exactly 1,048,576 bytes charged to its process gate. Probe sources, raw logs, artifact hashes,
collector output and results remain in
`/shared/present/bulk-file-reads/qualification/`; no probe/framework is merged.

Native qualification remains open: orchestrator-scheduled vela HWP live
RAM-npfs, followed by a separately approved owned NVMe-npfs workspace. Reuse the
same fixture and controls. Unsupported native backings refuse; libc/Go retain
ordinary reads. Copies still exist, each live layer charges its owned buffer,
provider private storage is ordinary process memory, and worker/device limits
remain. No ABI-changing PR merges before the 0.0.9 tag.
