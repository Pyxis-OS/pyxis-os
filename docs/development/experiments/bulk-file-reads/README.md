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
receipts and wrong-reply refusal. A separate 22-check process probe confirmed
provider normal exit, forced caller retirement with a retained receipt, and
exit with 16 unACKed native bulk completions; subsequent backing/budget use
remained available. An intentionally wrong repository archive hash refused
installation, left the
selected set unchanged, and its final verification passed. A separate read-only
GDB inspection observed
a 1,048,576-byte native request returning an owned buffer of that capacity,
with exactly 1,048,576 bytes charged to its process gate. Probe sources, raw logs, artifact hashes,
collector output and results remain in
`/shared/present/bulk-file-reads/qualification/`; no probe/framework is merged.

Native live RAM-npfs passed in the run below. Separately approved owned
NVMe-npfs qualification remains open. Reuse the same fixture and controls. Unsupported native backings refuse; libc/Go retain
ordinary reads. Copies still exist, each live layer charges its owned buffer,
provider private storage is ordinary process memory, and worker/device limits
remain. The 0.0.9 tag (`c02a2bfa`) lifted the merge gate on 2026-10-26.


## Post-0.0.9 rebase (2026-10-26)

Rebased the foundation onto Pyxis `09a19c94` and userland onto `81ee85f`.
The userland bulk head is `71e7baad`; its five patches are unchanged by the
rebase. The foundation patches also replayed unchanged. Release-gate text was
removed after the 0.0.9 tag, without adding a development ABI identity.

The rebuilt kernel/focused SDK/runtime, httpfs and pkg passed with the same
Clang 23 builder. The ABI check preserved every header line from both 0.0.8
and 0.0.9. Rebuilt private probes passed 98 native/transport, 80 lifetime and
22 process-retirement checks on four-vCPU nested KVM (`host,+invtsc`, q35,
modern VirtIO storage/rng, 2 GiB per probe). An 8 GiB session on that kernel
installed the 268 MiB/1242-file fixture through httpfs and RAM-npfs, verified
the selected set, then refused an archive hash mismatch without changing the
selection and verified it again. This was a correctness rerun; the historical
matched timings above were not remeasured or relabelled as post-release data.
Logs and artifact hashes are in the record's private evidence directory under
`rebase-009/`. Native live RAM-npfs passed below; native NVMe-npfs remains open.


## Native vela (2026-10-26)

Chronium ran the sealed XSAVE packet on vela: kernel source `23be575c`,
submitted as `1605b216` in #1246, userland `71e7baad`; Intel HWP applied on
16 CPUs. Kernel/runtime, httpfs, SHA engine, grants and fixture were shared by
A and B. A is ordinary-read pkg, B its explicit bulk-read replacement.
The live package and HTTP backing is RAM-npfs; this is not an NVMe measurement.
The fixture remains 281,031,168 bytes and 1242 synthetic files; fresh
`a1/b1/a2/b2` identities made each extraction real. Every install passed the
archive/member checks and 1242-file extraction, and its selected-set verification
passed with exit status zero.

| Run | Client | Host-observed install span, s |
| --- | --- | ---: |
| A1 | ordinary READ | 12.654 |
| B1 | bulk READ | 7.836 |
| A2 | ordinary READ | 12.403 |
| B2 | bulk READ | 7.843 |

Spans run from the first Remote output event to typed command completion.
They include repository/fetch/verification/extraction/publication work plus
transport and scheduling; they are neither target-clock timings nor isolated
bulk-read costs. No memory/FILE scan control, per-phase CPU profile, new request
count measurement or AC-state capture accompanied this run. No native switching
performance claim follows from the XSAVE functional pass in the same window.
Raw output and host events remain private at
`/shared/present/xsave-avx/native/results-20261026T102237/`.
Native owned NVMe-npfs remains unqualified.
