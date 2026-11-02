# Pool and image creation defaults — 2026-11-02

Disposable QEMU checks of the [accepted defaults](../../interfaces/limits.md#pool-and-image-defaults).
No native disks or ESPs were used. The RAM observations compare the unchanged
fixed-capacity baseline with the accepted automatic mode and numeric overrides.

## Inputs and configuration

Candidate runtime `155cecf23265695cc5de5e204a47c50c4f08e53d`, userland
`bc8266084d8dd1ce21684c3053992f0e30ea40df`; parent baseline
`8a1fa891898bcfd069ca088f972b322a60c26d5e`, userland baseline
`4da7da6284c9de0dad27e58f5f6e5cef370a99c4`. Filesystem, ports and lwIP pins
were unchanged. Builds used `pyxis-llvm23.1.3-8bdf136` and the candidate SDK.
The RAM implementation was built and booted at runtime
`1016ee15cc0ad6ba8ee4d0b49d0da95d633b2e88`, userland
`c44585ec9d2f59344b4da15a1be8c4c6a17f2029`, on parent main
`ea4f42eb17febf8a1ce9bce4a12f06a123932270`.

QEMU used KVM, q35, four virtual CPUs, 4 GiB RAM, OVMF, VirtIO block/network,
disposable sparse disks and user networking. The 4Kn case set both VirtIO block
sector sizes to 4096; other cases used 512-byte sectors. RAM observations used
the full live distribution at 2 GiB and 8 GiB, with GDB inspection after the
development space started. Core installer images do not start that live pool.

The development package catalog was built from these inputs and served locally
as the explicitly unsigned `wp7` repository, with the matching development ABI.
Released binaries were not made compatible with that ABI. The old-geometry
control changed only the installer and installer-init programs to genuine
pre-change baseline builds; all other program/kernel/profile inputs stayed at
the candidate. This made a real 512 MiB installation that the new installer
could Update through the current package protocol.

Commands used the ordinary image/base-package build, `tools/qemu-image.py build`
and `verify`, and the interactive installer through QMP. Local records and
screenshots are under `~/src/epsilon-wp7-audit-20261102/`; no checkout, build tree
or test disk was placed in shared handoff storage.

## Results

| Case | Observed result |
| --- | --- |
| Fresh 128 GiB Classic disk | Real Install completed; ESP is 1 GiB at 1 MiB, followed by the pool; structural verification and two installed boots passed |
| Fresh 4Kn Classic disk | Real Install completed on an 8 GiB disk; ESP is 1 GiB and FAT sector size is 4096; two installed boots passed |
| Existing 512 MiB Classic ESP | New installer recognized and completed Update of the 8 GiB control; both GPT copies remained byte-identical, ESP/pool extents unchanged; two updated boots passed |
| Coexist whole-gap default | 96 GiB scratch disk with a 600 MiB FAT32 ESP and a foreign 1 GiB sentinel partition: prefill and Review selected the independently checked 96,678 MiB gap, leaving zero tail |
| Coexist explicit smaller choice | Same gap, explicit 32 GiB pool; Review retained 67,014,492,160 bytes of unallocated tail |
| Too-small Classic disk | A 1 GiB disk was refused with required 1,075,909,120 and available 1,073,741,824 bytes; no smaller ESP fallback |
| Previously published 32 GiB disk | Original 0.0.10 image hash matched the release record; the revised standalone verifier accepted it and both boots passed |
| Live baseline, 2 GiB guest | Allocator capacity 2,007,711,744 bytes; logical npfs RAM pool remained the configured 2,147,483,648 bytes |
| Live baseline, 8 GiB guest | Allocator capacity 8,449,634,304 bytes; logical pool remained 2,147,483,648 bytes |
| Auto, 2 GiB guest | Allocator capacity and created pool both 2,007,711,744 bytes; pool free space 2,006,503,424 bytes |
| Auto, 8 GiB guest | Allocator capacity and created pool both 8,449,630,208 bytes; pool free space 8,448,225,280 bytes |
| Numeric override below the former floor | Explicit 4,194,304 bytes opened successfully; free space 3,043,328 bytes |
| Numeric override above the former ceiling | Explicit 85,899,345,920 bytes (80 GiB) opened in a 2 GiB guest; free space 85,895,577,600 bytes |
| Below the format minimum | 1,105,920 bytes (270 blocks) returned `CALL_LIMIT` for the requested RAM volumes; no RAM pool was opened; ordinary live spaces still started |

The current layout needs at least 271 blocks (1,110,016 bytes): two headers,
one bitmap block, eight volume-table blocks, the existing 258-block RAM journal
and two data blocks for the first volume's inode file and root directory. This
is derived at admission, not another authored minimum. Further roots and user
data still consume real pool space. The same kernel was used for all five RAM
cases; only guest memory or the external `LIVE_CONFIG` changed. GDB read the
allocator and actual opened-pool fields after normal startup. The kernel also
reported automatic versus configured capacity; no whole-capacity physical
reservation was made.

Both Coexist cases completed Review and a read-only plan dry run, with QMP disk
write bytes zero and unchanged hashes of both GPT regions and the foreign
sentinel. They did not apply the GPT append or format a pool. The too-small case
also recorded zero disk write bytes. These are selection/refusal checks, not new
qualification of the Coexist write executor.

The new compressed 128 GiB qcow2 was **48,496,640 file bytes**; the verifier
reported **48,373,760 allocated host bytes**. The old 32 GiB download was
**48,168,960 bytes**. These particular sparse artifacts do not reserve their
virtual capacity on the host; future guest writes can consume it. No throughput
or timing claim is made.

Installed checks ran through real boot and package commands. On the development
catalog, `pkg upgrade` honestly reported no newer release; no cross-release
upgrade or failure-recovery result is claimed. Rebuilding a damaged ESP, native
4Kn hardware, actual ESP staging exhaustion and physical-memory exhaustion were
not exercised here. Numeric Lua configuration is bounded by its integer
representation; native callers retain the unsigned request field. Neither has
the former 64 GiB policy ceiling.
