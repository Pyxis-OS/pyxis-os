# Native hard links: durability qualification

2026-10-19. Merged implementation Pyxis `76094b18`, fs `d2fb97c5`, userland
`4e9770b`. Follow-up userland `1836d2a` is integrated at `c365ccdc` (CI3204).
Existing `pyxis-llvm23.1.3-3d69e83`; no compiler/container change. Native result
and milestone completion remain pending.

## Configuration and commands

Ordinary `make -j16 kernel fs-tools` and `make -j16 sdk` passed with the published
compiler; the changed `ln` built against that SDK. CI3204 rebuilt the full image.
QEMU: q35/KVM, OVMF, CPU=max, four CPUs (one for replay/restart), 4 GiB,
modern VirtIO block/net, `cache=writeback`, local `/home` Btrfs backing. Disposable
130 MiB GPT media contains a 128 MiB NPFS pool and 8 MiB journal. No shipped
probe, kernel write-path change or owner media access.

Private source, commands, build logs, Remote transcripts and GDB scripts are in
`/shared/present/npfs-hard-links-task4/`. The hand-run `linkcheck` modes `seed`,
`publish`, `verify`, `lifetime`, `replay-link`, `verify-replay` and the labelled
`limit-guard-only` use ordinary public interfaces. Preliminary harness runs that
required a missing fixture volume are excluded. Timing runs have no debugger.

## Durability and lifetime observations

Four-CPU checks passed shared data/identity/counts, nonfinal unlink and alias
move, shared shrink, last unlink with two held files, held read/write at count
zero, final release, and both REPLACE/NO_REPLACE renames between aliases
preserving the two names. Stable publication linked a new version to a temporary
name, renamed it over `active`, then explicitly synced; the held old file retained
its old bytes. A one-CPU restart verified both published names, count two, held
identity and all 65,536 expected bytes. Stopped-pool fsck passed.

GDB stopped a real LINK transaction at `checkpoint` entry after `commit` flushed
its COMMITTED control, before checkpoint home writes: sequence 8, two redo
images, healthy pool. Copied the quiescent private disk while stopped, then let
the ordinary operation checkpoint. Read-only host fsck refused the COMMITTED
copy; writable replay/fsck on an independent copy passed. A separate one-CPU
kernel boot replayed the untouched COMMITTED image, verified alias/count/data
and prior stable names, then passed stopped fsck. Host replay, normal checkpoint
and kernel replay produced byte-identical pool images (SHA-256 recorded locally).
Extracted old/new bytes matched independently generated patterns. This is a
controlled durable-journal capture, not native power-loss qualification.

For count-limit guard qualification only, GDB temporarily set a held inode's live
count to UINT32_MAX at the comparison instruction. LINK returned LIMIT; staged
image count, sequence, allocation counters, both parent generations and feature
mask remained unchanged. Restored the original count before user execution;
the alias remained absent, INFO returned one, and stopped fsck passed. This does
not claim a structurally valid pool with billions of names.

## Task-3 review follow-up

`ln` destination `stat` could GET a provider FILE before LINK refused it. The
follow-up uses directory-only lookup with zero child rights and removes the
source diagnostic `stat`. Four-CPU QEMU HTTP fixture: control GET `/control`
arrived; old `ln` reproduced GET `/before`; fixed single-source `/after` and
multiple-source `/multi` refused with no requests. Native directory linking,
bytes and sync passed. Existing SDK/libc LINK has no provider OPEN fallback.

## Matched nested measurements

A = original pre-writer baseline `6e83cfa6` (exact CI3168); B = `76094b18`.
Archives match their respective metadata ABI; no identical-archive claim across
48→56 bytes. Same compiler, VM configuration, seed and local disk backing. After
boot/Remote, run three existing Lua batches of 100 create/write/close/rename/remove
operations, then `iobench write data://grow.bin --buffer 4080 --rounds 15 --sync`
and the same prepared-overwrite command with `--prepared`. Each has warmup and
15 verified samples. Remote batch proxies include launch/client scheduling.

| Boot | Namespace batches ms | Grow median [range] ms | Grow sync ms | Prepared median [range] ms | Prepared sync ms |
| --- | --- | --- | ---: | --- | ---: |
| A1 | 378.2 / 382.0 / 324.1 | 154.9 [151.1–196.8] | 31.6 | 154.5 [149.3–305.2] | 21.2 |
| B1 | 327.6 / 328.4 / 323.4 | 138.0 [130.1–147.6] | 42.3 | 132.4 [124.1–138.6] | 23.7 |
| A2 | 352.6 / 341.0 / 333.8 | 136.1 [133.2–156.6] | 41.6 | 127.1 [124.4–135.4] | 20.5 |
| B2 | 338.5 / 328.9 / 327.7 | 135.8 [126.7–145.3] | 41.9 | 125.9 [124.0–131.0] | 19.6 |

The later A/B transfer samples and sync intervals overlap; A1 has larger
variation. These runs do not isolate a transfer regression or establish a
speedup. Shared-host/nested scheduling and different matching archive revisions
limit attribution. All workloads returned success and passed stopped fsck; raw
sample/sync ranges remain in the transcripts. No native A/B claim.

## Native packet, pending owner approval/scheduling

`/shared/present/npfs-hard-links-native/` proposes ThinkPad PXE with a separate
disposable USB pool formatted feature-enabled, selected by its own GPT GUID.
No installed-pool conversion or PXE staging has been performed. The run sheet
requires explicit disposable-media approval; it checks stable publication,
held-file lifetime and an ordinary reboot/reopen through Remote. The matching
CI3204 image and private public-API tool are packet inputs. Native success is
not yet claimed. After that result, complete the task and move the concise
implemented milestone contract out of WIP.

INFO's 48→56-byte change requires independently published toolchain bundles to
be refreshed against the new SDK; rebuilding the integrated image alone does
not refresh those bundles. No compiler-container rebuild is required.
