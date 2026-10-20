# Explicit-durability measurements

Task 4 completed on 2026-10-20. Implemented contracts are in
[explicit durability](../../devices/explicit-durability.md). Earlier nested
results and crash checks remain in the [task-1/2/2b record](explicit-durability-baseline.md)
and [FAT32 task-3 record](fat32-batched-ordering.md).

## Native ThinkPad policy comparison

Owner-run 2026-10-20, approximately 16:00–16:45, four fresh PXE boots in ABBA
order. The sole disposable 32 GB SanDisk was directly attached to the ThinkPad,
with NVMe writes disabled. AC was online before/after every sample; that does
not prove uninterrupted AC between queries. A quiet pause preceded each run.
No debugger, profiler or per-write logging ran during native timing.

| Policy | Kernel | fs |
| --- | --- | --- |
| A, private/never merged | `69bfdd0266da` | `693e012ba5e4` |
| B, frozen main | `df458bb3c682` | `f3f22b6e07e7` |

A restores per-operation npfs namespace/reclaim/writeback commits and direct
ordered FAT32 plans on B's source. Both share USB/device code, generated kernel
configuration, userland `b6c709df5b67`, SDK and the published
`pyxis-llvm23.1.3-3d69e83` compiler. This isolates barrier-policy cost, **not all
historical implementation differences**: allocator safety, hard links, RAM pools,
file error ownership and modern cache/plan preparation remain on both sides.
Private source patches, build recipes and artifact hashes are preserved in
`/shared/present/durability-thinkpad-cheap/`; no measurement code merges.

The paced Remote controller ran once per boot:

- `pkg install clang-hosted@57dfcf9a33ccc0b366db6f0a7fbd5ca15b77ab5a16770401189d4cfcd0fa15e8`
  from horse's public repository, followed by `pkg verify`. Archive:
  372,762,112 bytes, SHA-256
  `ef776f7db289ace2b61eac1f12eb72fed7f97574635dd61d12067c5652fe7c2e`.
  Fetch was timed separately. Extraction starts after archive/index/inventory
  validation and ends after the existing final directory sync; the record is
  emitted only after successful command/lock cleanup.
- A declared-authority helper copied the same three frozen files, 110,939,596
  bytes, to a new FAT32 child and synchronized files/directories. Every output
  byte was compared with the source outside timing.
- Helpers removed the marked package tree and FAT32 child and synchronized
  cleanup. The package root remained marker-only; the FAT32 child was absent.
  Cleanup is namespace removal, not the retired Update revision selector.

No fixture restores occurred between boots. Package storage was the scratch
pool's home volume root, claimed only when empty or exact-marker-only; session
home was separate RAM. Private startup carried only the declared FAT32 source
through trusted Remote handoffs. Initial attempts on the preceding non-packet
image stopped at a missing read-only boot-ID file, before reboot/workload;
the explicit initial-entry command corrected this. All four accepted samples
had distinct boot identities, expected kernel revisions, status-zero commands,
matching staged-file identities and successful verification/cleanup.

| Envelope, seconds | A1 | B1 | B2 | A2 | B/A mean |
| --- | ---: | ---: | ---: | ---: | ---: |
| npfs package extraction + sync | 441.617 | 78.545 | 76.192 | 455.053 | 0.173 |
| FAT32 staged copy + sync | 11.606 | 9.497 | 10.075 | 12.099 | 0.826 |
| npfs owned-tree cleanup + sync | 232.650 | 32.523 | 25.145 | 236.964 | 0.123 |
| FAT32 owned-child cleanup + sync | 0.854 | 0.821 | 0.675 | 1.142 | 0.749 |

Extraction's mean fell about 83%; FAT32 staging's about 17%. Both primary
ranges are below A's ranges, with no accepted regression threshold reached.
Cleanup is descriptive. Fetch ranged 5.85–10.92 s and is excluded from the
primary envelopes, as are PXE/TFTP, host waiting and Remote collection. Two
samples per policy establish this workload's observed ranges, not statistical
confidence, cold-cache equivalence or a universal speedup.

Raw JSONL, successful commands, `accepted.json` and `comparison.json` stay in the
packet's `results/{A1,B1,B2,A2}/`. The orchestrator restored the saved PXE menu;
its SHA-256 was independently matched byte-for-byte. Staged A/B PXE payloads
were removed. `return-normal` requested restart; ordinary-image return was not
independently confirmed because the available Tailscale SSH session required
reapproval. No further reset was attempted.

## Owner-observed Update (0.0.6 to 0.0.7)

During the native 0.0.6 to 0.0.7 Update on the owner's stick, the owner read the
installer's progress display: the copy rate was about 1 MiB/s on earlier Updates
and 18 MiB/s on this one. This is one observation of the installer's own display,
not a matched measurement: the payload and cache state were not recorded
for the earlier Updates.

## Qualification and comparison limits

The packet passed four-CPU nested-KVM QEMU checks before native use: B then A
on the same xHCI USB image without restoration, miniature package install/verify,
full 110,939,596-byte FAT32 copy/byte verification, both synced cleanups and
Remote reboot with a fresh boot identity. GDB inspected BSP/xHCI activity during
copying. These clocks are correctness-only; this is not a full-Clang nested
extraction measurement.

The [ThinkPad USB reference](usb-superspeed-transfers.md) used the re-flashed
32 GB SanDisk. Its transfer shapes, payloads and cache state differ; it supplies
hardware context, not a matched durability denominator. Task 1's timing tables
are nested QEMU results, not a pre-existing native baseline.

Historical comparisons retain their actual scope:

- Task-1/2/2b used VM-local Btrfs; task 3 used shared FUSE. They are adjacent
  comparisons, not one matched four-state timing table. Earlier extraction
  controls have one sample, Update pairs three; published task-2b observations
  predate the later staged-home reuse correction.
- Legacy native `a29372f8` to task-2b also changes xHCI/core paths. Its observations
  remain USB-confounded. The clean replacement `321c4791` to `89c86f74` passed
  exact-geometry QEMU Update/restart, all 2,240 copied programs and clean fsck,
  but its planned repeated native Update comparison was superseded.
- The frozen FAT32 pair `4c97c87b` (fs `d2fb97c5`) and `4e21c105` (fs `f3f22b6e`)
  used matching later callers. `4c97c87b` differs from reachable `19c3cfc8` only
  in documentation; `016e12bc` still pins the old fs and is not the measured
  candidate's complete identity. The task-3 record has three samples per side.

The Fedora/BootNext/restore campaign was superseded by owner direction because
the Update path retires with the 0.1.0 installer work. Its fixtures, private hooks,
failed ledgers and local controller checks remain under
`/shared/present/durability-handsfree*/`; it supplies no accepted native timing
samples. Additional Update/extraction repeats were not completed. The accepted
replacement above closes task 4 with these coverage limits; it does not qualify
full installer performance, all media, fragmentation improvements, arbitrary
power loss or durability when a device dishonestly acknowledges flush.
