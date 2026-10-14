# FAT32 mounted mirror repair qualification

Owner accepted both [scope defaults](../../wip/fat32-preflight-cost.md#assigned-follow-up-fat-mirror-repair)
on 2026-10-14. No real ESP writes are authorized.

## Baseline before implementation

Untouched main `ba5e134d054faa60ef2178e2d046dccaf9342a38`, fs `d84f8084`,
userland `220b8076`, ports `efe8d877`, lwIP `a1aadb91`. Exact-main CI API run
2578 (UI run 2491) supplies verified kernel/SDK/userland/ports bundles; each
manifest matches these revisions and payload verification passes. Kernel
SHA-256 `8a429bc0ac764a481a7d8427a19ba2030bb667ea1e04b1ef4437fea68bdaa208`.
Existing e0801c2 builder; no compiler rebuild. Earlier artifacts fetched with
the UI number as an API ID were rejected by their revision manifests before
any FAT operation; they supply no baseline sample.

Q35/nested KVM/host CPU, four CPUs, 1 GiB, fresh OVMF, modern VirtIO block,
`cache=none`, no NIC/USB. Pristine GPT scratch partition: 128 MiB, 512-byte
clusters, two FATs, 258,078 clusters; same fixture as the preflight qualification.
Boot the trusted `init-fat32-mount-write.pxe boot.install=1`, explicitly select
disk 1/GPT entry 1. Read-only GDB boundary observers (`may-call-functions off`)
record request admission deltas and host monotonic elapsed time. Debugger
stops perturb time; nested spans overlap. Host load remains unisolated. These
are profiled functional/count comparisons, not native throughput.

Sequence: CREATE `/a`, `/b`, `/c`; hold `/a` RW; write `abc` at offset zero three
times; FILE sync; BEGIN/CHECK three times; drop; quit (ends the interval and
retires). No media edits or function calls through GDB.

| Operation | Block admissions | Host milliseconds |
| --- | --- | --- |
| RW admission | 100 | 58.56 |
| CREATE (3) | 25 / 20 / 20 | 28.85 / 30.85 / 31.82 |
| three-byte WRITE (3) | 24 / 21 / 21 | 39.52 / 35.28 / 33.67 |
| FILE sync | 1 | 4.72 |
| CHECK (3) | 39 / 39 / 39 | 30.22 / 30.21 / 30.96 |
| RETIRE | 12 | 18.33 |

All measured operations succeed; detached `fsck.fat -n` is clean, with five
files/three allocated clusters. One manually entered END used an incorrect
opaque identifier and is excluded from the comparison; quit performs the valid
END before successful retirement. Raw fixture, commands and GDB captures stay
outside the repo under `/shared/present/fat32-mirror-repair/baseline/`.
All baseline QEMU/GDB jobs are stopped. Implementation/after qualification pending.
