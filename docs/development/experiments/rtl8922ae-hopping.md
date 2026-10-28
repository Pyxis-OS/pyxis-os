# RTL8922AE bounded hopping qualification

Implementation checkpoint, native qualification pending. The accepted contract
is in the [milestone](../../wip/rtl8922ae-wifi.md#channel-hopping--accepted-chronium-2026-10-28).
No native/PXE action is performed by these source/host checks.

## Inputs and baseline

Baseline kernel main `750cd0b99e685c07eb55f9ffe0fd7db1cf83da1d`, built before
implementation with published `pyxis-llvm23.1.3-8bdf136`. Four-CPU q35/KVM,
host CPU, 4 GiB, OVMF, virtio/slirp, no radio or disk. The unchanged clean
qualification archive pins userland `1b546c9f`; only the new baseline kernel is
substituted. Actual running kernel and archive marker were checked via Remote;
GDB observed idle/absent radio, and owned processes retired through QMP.
Three existing iobench invocations each ran a warmup and three verified 1 MiB
RAM-archive reads, buffer 4096. Raw baseline and later matched control records
stay local under `rtl8922ae-hopping-20261028/`; the final PR/packet identifies
exact candidate and userland heads. Integration subsequently incorporates the
merged poll stack; these controls do not isolate a radio-performance effect.

## CPU-only checks

Reference Linux v6.18 `7d0a66e4bb9081d75c82ec4957c50034cb0ea449` defines
BE descriptors, PPDU MAC/PHY prefixes, IE01 and normalized current-group matching.
Transient manual wire fixtures passed 24 cases / 2,795 checks under plain GCC
and ASan/UBSan, with no sanitizer finding. Inputs were overwritten/freed after
each callback. Cases cover channels 1/6, per-BB isolation, variable prefixes,
missing/invalid/late/unrequested reports, no report cache, fixed-channel fallback,
reset/stop, and pending/final capacity while retaining fitting records.

The 513-candidate case keeps 128 records, counts one pending-capacity drop and
384 final-capacity drops: 385 total. Existing final records still merge when
full. No firmware, MMIO, DMA or RF is simulated. FILE serialization is separately
source-inspected; native publication remains a later qualification gate.

The pinned firmware and accepted world/SAR-16 tables prepare compatible 1/6
nonlimit controls. Their signed common normal/RU/path image equals the prior
channel-1 image, including SAR 64/64 quarter-dBm. Existing regulatory regression
still admits 296 / refuses 16 cases with zero numeric ceiling violations; invalid
common-mask/input preparation clears its output. This proves CPU construction,
not physical RF enforcement.

## Limits and next evidence

The immutable result grows from 16 to 24 header bytes in the development ABI;
flags/drop count persist after operation close. There is no legacy-format shim.
SCAN STATUS stays its existing size. Kernel, SDK, CLI and private helper must
match; no retained release ABI is listed by this development tree.

Native qualification must establish firmware ENTER/END for 1+6, actual matched
PPDU observations where an AP is heard, common-image endpoint checks, retained
same-mask repetition, unchanged first FILE and independent close lifetimes,
rfkill observations and wired management, followed by byte-exact PXE restoration.
No nearby AP or per-channel nonempty result is guaranteed. No radiated-power,
physical-rfkill, association, retry/recovery or full-mask claim.
