# Git slice 2 qualification

2026-10-24: userland `f3fa4b5a30af967a2c46fb59bbf6cf4c6de5d88d`, Go
`f93462692a63de8d56cc3059e94f558f253fbbbb`; unchanged pinned go-git v5.19.3 /
go-billy v5.9.2 with the recorded patches. Linux and Pyxis builds pass fixed
vendoring/offline compilation and retain source hashes and complete notices.
Final P1F SHA-256:
`ca6704e3d60a47ddc8357b33a99971eaf54cfcf1856718fbb6b5d5870b1b8c82`.

## Configuration and commands

The [slice 1](../git-slice1/README.md) merged source and clone/status record are
the baseline. No kernel/ABI or compiler change is needed. Reuse its saved core
`e57d9701e051` image in nested KVM/Q35, `max,+invtsc`, four vCPUs, 4 GiB,
VirtIO-SCSI read-only ISO and modern VirtIO net/RNG. Home is RAM-npfs in the
2 GiB logical live pool. Install the ordinary development Git package, copy its
bundle into writable home and update the entry/provenance through normal file
operations. The final entry's SHA-256 is checked against the build artifact.
No package ABI metadata is relabelled to admit a different kernel.

Clone `https://github.com/git-fixtures/symlinks.git`, HEAD
`23c603542a49654726bc2c14c4b778f8c63f2862`. Use ordinary shell file operations
and literal `[user]` name/email at `home://.config/git/config`, then:

```sh
cd home://work/sub/deep
home://git-dev.pxb log -n 1
home://git-dev.pxb add item
home://git-dev.pxb diff --cached
home://git-dev.pxb commit -m "native local commit"
home://git-dev.pxb status
```

Repeat for edits, tracked removal and `add -A`. The final build repeats a
stage/diff/commit/clean-status cycle at resulting commit
`bbca9d8c3003aa7fdec0e3800e45edcab4b0b283`.

## Results

| Check | Observed result |
| --- | --- |
| Nested cwd and nearest repository | Log/status find the parent repository. Add stages `sub/deep/item`, not root `item`. A nested real repository selects its own original HEAD; a `.git` file refuses without climbing past it. |
| Retained ancestry after rename | Rename `home://work` while the shell holds its descendant; subsequent log, add and commit work through held objects. |
| Authority boundary / read-only | Discovery from `boot://share` stops at the granted ancestor boundary. Read-only archived log/status/diff pass; add and commit refuse creating their locks. |
| Staging and text diffs | Explicit add plus all-tree add stages additions, same-size edits, tracked deletions and `.gitignore`; ignored new content is omitted. Staged and unstaged versions differ correctly, including `MM` status. |
| Binary and modes | Binary marker appears; edited target text retains mode 120000. A Linux-prepared valid executable index entry remains 100755 after native add/commit. New files are 100644. |
| Identity/time | Missing and duplicate identity refuse. Signatures use configured `Europe/Bucharest` (`+0300`) for both author and committer; invalid `TZ=Local` refuses explicitly. |
| Commit refusals | Empty index change, detached HEAD, a standard conflicted index and an existing index lock refuse; explicit cleanup restores normal operation. |
| Nested repository staging | Refuses rather than flattening it; index SHA-256 before/after is identical. |
| Offline workflow | A copied manifest omitting TCP/DNS/service grants still stages, commits, diffs and reads history. |
| Independent Linux inspection | Copy native `.git` bytes read-only through Remote. Linux `fsck --full --no-reflogs` reports no errors; log, trees and index confirm parents, signatures, mode 120000/100755 preservation and expected content. One unreachable superseded staged blob is expected. |
| Debugger | Read-only GDB inspection shows all four CPUs waiting in the scheduler; detach resumes normally. |

The first staging attempt exposed an upstream unguarded optional Unix-stat
helper on Pyxis. Mapping the fault through its carried Go pclntab identified
`doUpdateFileToIndex`; the matching checkout guard fixes it. The interrupted
attempt's lock was explicitly removed before qualification repeated. The final
build has no such fault; no successful no-op metadata helper was introduced.
A second small upstream correction propagates subtree read errors instead of
turning them into successful EOF.

Raw builds, Remote transcripts, copied Git metadata and Linux inspection are
under `/shared/present/git/slice2/`; no raw captures or scripts are committed.
No native/PXE, persistent-device performance, concurrent-writer stress or
power-cut run is claimed. Existing sync-on-close remains unchanged; native
vela cost measurement is a separate follow-up. `$EDITOR` message entry and
authenticated transfer remain later work. No public package publication.
