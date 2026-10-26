# Native atomic append

Status: assigned to codex-beta (chronium, 2026-10-26). The first-round contract
is accepted. Implementation follows the baseline; the plain-output cursor
decision below remains open. This document describes planned work, not delivered
behaviour.

## Accepted contract (chronium, 2026-10-26)

`FILE_APPEND` chooses EOF and writes under one backing-object mutation owner.
It requires existing FILE WRITE, not a new right or kernel file cursor. Its
reply gives the selected start offset and confirmed count. WRITE still permits
positioned overwrite, resize and sync: this is not append-only authority.

One native operation carries at most **4,080 bytes**. Nonempty success reports
`1..requested`, with a contiguous confirmed prefix at the selected EOF. A caller
may append the known remaining suffix as a new operation; other writers may
interleave. Larger writes, flushes and formatted lines are not whole-record
atomic. Errors give no usable count and are not automatically replayed; a lost
or malformed mutation reply is OUTCOME_UNKNOWN. Retained capture budget failure
keeps the existing immutable TOO_LARGE/no-prefix contract.

A zero-length operation validates authority/support and returns EOF plus zero
without mutation or capture byte-mode selection. It does not reserve later
success. Reads, seek, descriptor aliasing and explicit positioned writes retain
their existing semantics. Append writes update the local cursor from the reply.
`pwrite` remains positioned even on an append descriptor.

Support RAM, npfs (including RAM-backed npfs), writable FAT32 and native capture
byte producers. Serialize aliases/hard links through their canonical backing
owner, not a lock on one FILE wrapper. Preserve capture mixed-output/failure
latches, retention accounting, FAT32 owned-ESP gates and ordinary CUT/lineage
rules. Extend the existing native completion path without changing its
one-outstanding-operation-per-process limit.

HOST refuses APPEND with BAD_OPERATION/ENOTSUP until its host-side contract can
prove atomicity against external writers. HTTP/text providers continue refusing
append; there is no POST/PATCH interpretation or SIZE+WRITE fallback. Unsupported
append must refuse before creating or truncating a target: native CREATE needs
an append-support requirement, and already-held/inherited FILEs use the zero
probe. Read-only and sealed objects retain their mutation refusals.

**Shell correction:** plain `>` truncates once and uses positioned/sequential
writes with its own cursor, never append policy. `> host://file` must keep
working; only `>> host://file` refuses. The cursor-sharing consequence for
descendants is the open task-level decision below.

Append success, `fflush` and close do not imply durability. Existing FILE_SYNC
and directory-sync contracts remain: RAM is not persistent, npfs has its
committed-journal guarantee, FAT32 is not journaled. No new disk format or
power-fail-atomic log-record promise.

## Replacement scope

| Current path | Change |
| --- | --- |
| libc `descriptor.c` append SIZE+WRITE; stdio `a`/`a+`, `fdopen`, O_APPEND | Use APPEND, preserve access selection/ownership/read-ahead, dup state and explicit-position APIs. |
| SDK `file.c`, native typed OPEN/CREATE, FILE/completion dispatch | Add bounded append and before-effects support admission. |
| `stdout.append` / `stderr.append`, shell redirects, capture and merge relays | Keep companion names; strengthen them to real APPEND. Plain `>` uses sequential positioned output. Preserve capture handoff and sticky failure rules. |
| Lua/libuv launch bridges, Neovim `:w >>` | Update inherited policy validation and verify libc-backed callers; no editor-specific append path. |
| sbase `tee` restriction | Restore `-a`; keep the independent signal-option refusal and output failure handling. |
| Go pinned syscall opens, inherited stdout/stderr and runtime/crash writers | Enable real append and remove every SIZE+WRITE/preflight workaround, including allocation-free runtime paths. |
| Rust writable-files work | Coordinate with its owner; `.append(true)` selects write access plus O_APPEND. Do not absorb unrelated writable-files work. |
| LLVM Support append refusal | Audit actual fork call sites, adapt append and retain unrelated mode/CRLF refusals. |

Source inspection baseline: parent `1d2f71e7`, userland `4060486d`, ports
`4f2e8aeb`, fs `f3f22b6e`; current docs base `e13b8f35` adds only controller docs.
Key references: [FILE ABI](../../include/abi/file.h),
[libc descriptors](../../userspace/libc/descriptor.c),
[typed OPEN](../../userspace/lib/resource.c),
[shell launch](../../userspace/shell/launch.c),
[capture producer](../../kernel/object/output.c),
[npfs worker](../../kernel/fs/npfs.c), [FAT32 worker](../../kernel/fs/fat32.c),
[append environment](../interfaces/environment.md#shell-selection-and-file-append).
The detailed inventory with fork revisions and line evidence is retained in
`/shared/present/atomic-append/PROPOSAL.md`; implementation PRs update the public
interfaces beside code, not that private proposal.

## Task-level decision: plain output across descendants

**Proposed default; awaiting chronium:** plain `>` uses the existing owned byte
relay to hold one positioned FILE cursor. Children and descendants receive its
PIPE; seeking is unavailable. Require the declared relay/pipe/launch grants
before opening or truncating any target, rather than inventing authority.
`>>` and capability captures use real APPEND.

Today [redirect_appends](../../userspace/shell/launch.c) deliberately passes
append to every redirected FILE because child processes do not share a userspace
cursor. Merely clearing that flag makes a script's second command start at zero
and overwrite its first command's output. The proposed relay preserves complete
script output and `> host://file` without a kernel cursor, but changes redirected
stdout from seekable FILE to PIPE and needs existing declared helper authority.
Do not silently adopt that change or an independent-cursor overwrite policy.

## Ordered tasks

- [ ] Capture before-code QEMU costs with existing tools, single runs unless a
  close result drives a decision; record matched revisions/configuration and
  commands below. No attended repeated sampling.
- [ ] Native APPEND, canonical RAM/npfs/FAT/capture serialization, required-support
  CREATE, SDK helper, completion/export integration and interface docs.
  After this task chronium can append concurrently through native capabilities.
- [ ] libc, inheritance, shell and dependent ports in linked PRs; restore tee
  append and remove the obsolete workarounds/debt. Resolve the plain-output
  decision before implementing that part.
  After this task ordinary append modes/redirection use one native operation.
- [ ] Go, Rust and LLVM adapters with focused fork PRs and affected prefix
  rebuilds. Report exact rebuild requirements when known; do not change the
  hosted compiler pin or rebuild its container implicitly.
  After this task supported language/compiler append users need no workaround.
- [ ] QEMU concurrency/alias/large-write/short-error/rights/CUT/retention checks,
  matched costs, then one quick hands-free native scratch-volume qualification.
  Check SYNC separately; record source-only limits honestly.
  After this task the milestone can close with the old append restrictions gone.

Publish dependency PRs first; parent pins their published exact heads. Rebase
fast-forward-only dependencies onto current main before reporting. No compatibility
shim or alternate non-atomic append surface remains after migration.

## Measurements

Before-code run is being prepared on `e13b8f35`, builder
`pyxis-llvm23.1.3-3d69e83`, QEMU/KVM with four vCPUs and 3 GiB RAM. Use existing
`iobench write` at 4,080 and 64-byte chunks on RAM-npfs and a disposable disk
npfs pool, plus an existing Lua interpreter's `io.open(..., 'a')`/write loop
timed with its native monotonic clock. Record loop flush policy and output
verification; no new benchmark program or infrastructure. After runs use the
same image profile, tools, scratch layout and commands. Raw material stays in
`/shared/present/atomic-append/baseline/`. Numbers are added when measured; none
are claimed yet. Native validation later is one quick hands-free run.

## You should know

Atomicity is per bounded native operation, not per `printf` or `fwrite`, and it
does not add durability or append-only security. HOST append will refuse;
ordinary HOST `>` remains supported. A possible PIPE-backed plain-redirection
transport is a new unresolved choice, not an accepted implementation default.
