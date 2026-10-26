# Rust native copy and recursive removal

2026-10-26. Delivers the accepted native `fs::copy` and `remove_dir_all`
contracts in the [std milestone](../../wip/rust-std.md#recursive-removal-and-copy--accepted-chronium-2026-10-26).
This is QEMU qualification, not native hardware or crash-durability evidence.
The first-profile close-out remains a separate task.

## Revisions and build

Baseline: exact Pyxis main `5c96f016`, userland `156924a0`, Rust `4be8bb57`
and libc crate `bd180991`, captured before edits. Candidate dependencies:
[userland #394](https://git.internal/PyxisOS/pyxis-userland/pulls/394) at
`fdfa26cb549e701022f0b4e5f6603e2d41c018c9`,
[libc #6](https://git.internal/PyxisOS/pyxis-libc/pulls/6) at
`805ff5ed2f7f88cfea9e5e1fec67e074e8ee8e56` and
[Rust #7](https://git.internal/PyxisOS/pyxis-rust/pulls/7) at
`1c82b7cac7de6e456741ec3084cfb42c30fa8e85`.
The integration rebuilds on Pyxis main `e9e9ecc2`; its inherited ports pin
is `345f02c4`. Filesystem `f3f22b6e` and lwIP `d52ebd20` stay unchanged.

Build kernel/runtime/SDK with `pyxis-builder:pyxis-llvm23.1.3-3d69e83`.
Rebuild the Linux-host compiler and host/Pyxis libraries on horse from the
pinned Rust fork, bootstrap and CI LLVM; Cargo uses the closed proxy and the
build runs offline. Export and audit the actual receipt/compiler hashes,
libc overlay and C signatures. All 26 native rlibs (506 ELF members) pass:
no embedded LLVM bitcode. Rebuild std main, workers, core+alloc hello,
tiny-skia and hexyl using that prefix/SDK. No compiler-container change,
new third-party dependency, new CI job or boot self-test was added.

Raw commands, private qualification programs, fixture hashes, build logs,
GDB observations and results: `/shared/present/rust-copy-remove/`.
`baseline-inputs.json`, `cost-inputs.json`, `artifact-audit/audit.json` and
`fresh-base-build.log` identify the inputs. Early runtime checks used
`4c894c49`; `5a0eb03` corrects self-copy error classification for non-UTF-8
names, and `649ad2c` bounds chunks by both native transfer limits. These
three userland commits rebase onto `68b8688` as the published head above.

## Functional qualification

QEMU q35/KVM, host CPU, 2 GiB, OVMF, VirtIO GPU/network/RNG; separate one-
and four-CPU runs. A confined virtio-fs directory provides HOST; home is RAM,
and scratch is disposable npfs. No real disk or ESP is involved.

- Public Rust copy passes new/existing/empty files, returned byte count,
  held destination identity and contents, source READ vs destination WRITE,
  absent-target CREATE, known self/hardlink refusal before resize, and failure
  preservation. Copy does not propagate a read-only grant or source timestamps;
  existing HOST inode/mode are preserved. RAM/HOST raw non-UTF-8 names work,
  including InvalidInput for a known self-copy.
- Recursive removal passes empty/missing/FILE operands, 151-entry trees and
  100-level depth, root/dot/dotdot and ordered-path guards, named cwd/ancestor
  deletion, held files/DirEntries after unlink, and four joined Rust workers
  repeatedly operating on separate trees.
- HOST root/child/dangling symlinks are unlinked without changing their external
  targets. Source/destination symlink copies and FIFO removal are refused.
  A real confined tmpfs submount is emptied, then its root removal returns
  ResourceBusy. No one-filesystem or rollback claim is made.
- A real completed provider FILE source copies through a READ-only OPEN.
  Scoped GDB observes zero provider OPEN/REMOVE calls for refused writer and
  recursive-removal routes, then exactly two read-source OPEN controls.
  The exported FILE reports a domain without an object ID: a distinct native
  domain proves separation. Existing destination WRITE without READ/CREATE/
  REMOVE works; an absent target without CREATE fails without publication.
- Concurrent growth of a 64 MiB source is ignored beyond its initial length;
  shrinking it after observed copy progress produces UnexpectedEof with a
  partial target. A real 1 MiB full tmpfs produces StorageFull and retains its
  partial copy. An 8,388,627-byte sparse HOST source occupies 16 blocks; its
  byte-identical copied target occupies 16,392 blocks. A 70,000-byte RAM name
  copies and recursively removes without an incidental fixed name cap.
- Four ordinary concurrent creator threads produce the real no-progress
  WouldBlock/EAGAIN result; after joining them, a separate explicit cleanup
  succeeds. This does not promise a total-time bound under ongoing progress.
- Earlier std/File/directory, managed-worker/TLS/synchronization, core hello
  and tiny-skia checks pass on one/four CPUs; raster checksum is
  `d0dfb9f8f80d6c59`. Rebuilt hexyl ASCII/no-colour output matches its pinned
  Linux reference byte-for-byte.

The final helper/SDK and rebuilt integration kernel repeat raw-name, public
RAM/npfs/HOST and source growth/shrink cases, plus one-CPU copy/removal and
four-CPU std/workers/hexyl regressions. The first repeated HOST regression
found its earlier fixture directory still present; preserved that output and
used a fresh directory before the passing repeat. No allocator/close-failure
injection, unknown-identity writable fixture, physical concurrent HOST writer
or power-loss test is claimed. Those error/cleanup branches were inspected.
Public File/Dir destructor close policy is unchanged.

## Matched ordinary-path controls

Frozen A/B/A/B fixtures have the same native executable bytes and archive
inventory; only the candidate kernel adds the typed HOST symlink operation.
Both archives are 41,601,536 bytes. This compares ordinary C paths, since the
old Rust conveniences were Unsupported. Each CPU count has two boots per
variant, three samples per boot. The directory control lists 32 entries 128
times, optionally querying path metadata for every entry. Launch control:
1,024 empty echo children after 16 warmups. Values are host Remote wall
milliseconds, median (minimum–maximum), six samples per cell.

| CPUs | Control | Baseline | Candidate |
| --- | --- | --- | --- |
| 1 | ram | 13.71 (13.25–14.07) | 13.89 (13.58–14.11) |
| 1 | ram + stat | 24.72 (24.22–25.54) | 25.17 (24.86–25.28) |
| 1 | npfs | 81.76 (81.04–82.14) | 82.54 (82.42–83.68) |
| 1 | npfs + stat | 180.10 (179.26–180.99) | 181.76 (181.55–182.39) |
| 1 | launch | 832.23 (823.02–877.84) | 854.72 (832.24–998.72) |
| 4 | ram | 19.97 (12.22–26.30) | 31.39 (11.56–131.74) |
| 4 | ram + stat | 42.27 (22.56–99.63) | 34.04 (28.45–115.60) |
| 4 | npfs | 137.59 (94.37–519.64) | 148.43 (99.13–204.91) |
| 4 | npfs + stat | 656.21 (351.30–1310.06) | 448.51 (379.53–523.76) |
| 4 | launch | 1166.64 (950.65–1277.40) | 971.74 (888.45–1272.34) |

One-CPU list/stat medians differ by about 1–2%; launch median rises 2.7%
with overlapping ranges. Four-CPU variation is too large to attribute a
regression or speedup. An earlier fully retained series under shared CPU
contention is in `costs-contended/`; it is not evidence of a performance gain.
The repeat began after horse returned idle, but its four-CPU samples remain
variable. `cost-vmstat.txt` and `cost-horse-vmstat.txt` retain load observations.
These frozen comparisons precede the unrelated VM/SDL rebase; functional
qualification, not timing attribution, is repeated on the new base.

GDB backing observations load each fixture's own kernel symbols. Baseline
and candidate agree exactly: idle allocation is 10,989 pages (one CPU) or
11,077 (four); one additional held Remote shell adds 392 pages (1.53 MiB),
and closing it leaves two pages above initial idle in both variants. The
390-page reclamation and retained two-page difference are ordinary-session
controls, not proof that every copy/remove failure is leak-free.

## User-visible limits

Copy changes the held destination directly and copies bytes/length only;
failures can leave it truncated or partial, and holes may consume backing.
Provider writers and unprovable existing-object distinctness are Unsupported.
Removal follows granted native directory reachability, without following
symlinks; final operations remain name-based and can race external renames.
Both stop at the first substantive error, preserve partial effects, check
helper-owned closes and imply neither rollback nor sync. See the
[mutation debt](https://git.internal/PyxisOS/pyxis-os/issues/1404).
