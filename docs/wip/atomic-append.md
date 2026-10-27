# Native atomic append

Status: assigned to codex-beta (chronium, 2026-10-26). The first-round contract
is accepted. Native APPEND/SDK and the libc/shell slice are built and qualified
in QEMU. Go and LLVM adapters are qualified; Rust migration and final native
qualification remain.

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
rules. Extend the existing native completion path without changing its current
[shared metadata allowance](../interfaces/native-completion.md#authority-and-admission).

HOST refuses APPEND with BAD_OPERATION/ENOTSUP until its host-side contract can
prove atomicity against external writers. HTTP/text providers continue refusing
append; there is no POST/PATCH interpretation or SIZE+WRITE fallback. Unsupported
append must refuse before creating or truncating a target: native CREATE needs
an append-support requirement, and already-held/inherited FILEs use the zero
probe. Read-only and sealed objects retain their mutation refusals.

**Shell output (chronium, 2026-10-26):** plain `>` truncates once, then gives
children a real FILE with append policy on RAM, npfs, FAT32 and capture backings.
This preserves output across descendants without a shared kernel cursor. Only
an APPEND-unsupported backing such as HOST uses the existing owned positioned
byte relay. Its children receive a PIPE, so seeking is unavailable and existing
declared helper/pipe/launch authority is required before creation or truncation.
`> host://file` remains supported; `>> host://file` refuses before effects.
Provider/transaction staging keeps its existing separate contract.

Append success, `fflush` and close do not imply durability. Existing FILE_SYNC
and directory-sync contracts remain: RAM is not persistent, npfs has its
committed-journal guarantee, FAT32 is not journaled. No new disk format or
power-fail-atomic log-record promise.

## Replacement scope

| Current path | Change |
| --- | --- |
| libc `descriptor.c` append SIZE+WRITE; stdio `a`/`a+`, `fdopen`, O_APPEND | Use APPEND, preserve access selection/ownership/read-ahead, dup state and explicit-position APIs. |
| SDK `file.c`, native typed OPEN/CREATE, FILE/completion dispatch | Add bounded append and before-effects support admission. |
| `stdout.append` / `stderr.append`, shell redirects, capture and merge relays | Keep companion names; strengthen them to real APPEND. Plain `>` uses an unsupported-backing relay only. Preserve capture handoff and sticky failure rules. |
| Lua/libuv launch bridges, Neovim `:w >>` | Update inherited policy validation and verify libc-backed callers; no editor-specific append path. |
| sbase `tee` restriction | Restore `-a`; keep the independent signal-option refusal and output failure handling. |
| Go pinned syscall opens, inherited stdout/stderr and runtime/crash writers | Enable real append and remove every SIZE+WRITE/preflight workaround, including allocation-free runtime paths. |
| Rust writable-files work | Coordinate with its owner; `.append(true)` selects write access plus O_APPEND. Do not absorb unrelated writable-files work. |
| LLVM Support append refusal | Audit actual fork call sites, adapt append and retain unrelated mode/CRLF refusals. |

Cross-checked against the append inventory in [Rust integration #1257](https://git.internal/PyxisOS/pyxis-os/pulls/1257).
Git's billy adapter (`userspace/git/fs.go`) forwards append to Go; with CREATE it
may create parent directories before Go rejects the flag, so that refusal is not
a before-all-effects guarantee for Git. spdlog and Chocolate Quake file logging
already use libc append and need their non-atomic documentation updated, without
adding atomic log rotation or durability promises.

Keep the independent protocol/profile refusals: libc append on PIPE, CONSOLE,
STREAM or TRANSACTION; direct non-FILE typed OPEN flags; stdin and non-FILE
append companions in startup/job/Lua/libuv handoffs; shell `>>` to provider or
transaction staging sinks; and read-only textfs. Libc's transaction type check is
after typed OPEN but before STAGE; native FILE append does not introduce a new
transaction contract. BusyBox `tar -r` remains outside its selected archive
profile: appending an archive requires header/terminator handling beyond EOF
placement. Migrate FILE admission without weakening these type/direction checks.

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

## Libc/shell integration

The held-directory `DIRECTORY_APPEND_SUPPORT` query lets the shell classify an
absent output before helper preparation or creation. It requires CREATE and
WRITE_FILES together, selects no name and reserves nothing. Actual creation uses
the retained parent and `DIRECTORY_CREATE_REQUIRE_APPEND` when required. There
is no backend inference from the URI or metadata identity domain.

The source changes replace libc, inherited-stream and byte-relay SIZE+WRITE with
APPEND, probe support before `fdopen` association, and prepare HOST relay grants
before redirect creation/truncation. Independent explicit redirects retain their
own cursors; lexical `2>&1` shares its selected sink. Background redirects remain
unsupported, including on later pipeline stages.

Repeated redirects to one initially absent native name keep independent bindings.
After an earlier redirect creates it, a later redirect may look it up once through
its own retained parent, only after matching parent identity and final component.
The returned live FILE identity must match the held created FILE; replacement,
missing comparison metadata, authority loss or lookup failure is final. No earlier
FILE grant substitutes for the later parent's authority, and no mutation is retried.

QEMU checks passed on four CPUs for RAM and RAM-/disk-backed npfs: stdio `a`/`a+`,
returned position, positioned `pwrite`, `fdopen` ownership/access, shared read-ahead,
dup aliases and four concurrent writers. Shell checks cover script descendants,
both `2>&1` orders, independent/reversed/mixed redirects to new targets, root
aliases, input-alias refusal, capture bytes/value handoff, failed captures, `tee -a`
and HOST refusal before create/truncate. HOST plain output and independent worker
cursors passed. A one-CPU guest passed FAT32's same libc checks and Lua/luv child
inheritance through both a native FILE and the HOST relay. FAT authority came from
a boot-declared source and an explicit bundle, not an ambient writable FAT root.

Fresh SDK (including C++ runtime), kernel, selected native application closure,
Lua 5.5.1, Lua 5.1/LPeg/luv, libuv/relay and all selected sbase tools build/link.
No native hardware, injected I/O/short-write fault or teardown heap-leak proof is
claimed. Existing append debts remain open until their remaining language and
native scope is resolved.

## Language adapters

Go's pinned syscall/runtime adapter uses APPEND for `O_APPEND`, inherited FILE
output, diagnostics and crash-output targets. Empty ordinary writes preserve the
cursor; `Pwrite` remains positioned. The runtime retains its allocation-free
serialized output buffer and nosplit path. Required-support CREATE or a held-FILE
zero probe refuses unsupported backing before create/truncate.

LLVM Support accepts `OF_Append` through the native-FILE libc route. Its upstream
Unix compatibility rule selects create-or-open without truncation, even with
CreateAlways/CreateNew. `raw_fd_ostream::pwrite` uses real positioned I/O, including
buffered and multi-operation patches; seeking around an ordinary write would
append instead. Staged VirtualOutput still refuses append because it publishes
by replacement. AtomicLineLogger retains its explicit refusal: arbitrary-length
whole-line atomicity exceeds the native operation contract. The PID-based
coverage-output refusal is independent and remains.

The Go prefix and affected Git/Caddy consumers were rebuilt with the existing
bootstrap. QEMU checks passed for Go on RAM/npfs/FAT32, concurrent writers,
inherited/runtime/crash output and HOST refusal; Git init also passed. Caddy was
built/linked but not run: its ordinary bundle requires TCP authority absent from
this fixture. LLVM's 55-archive development prefix and direct Support qualifier
build/link; QEMU covers RAM/npfs/FAT32 and HOST admission, append disposition,
positioned overwrite, buffered patches and multi-operation patches. No native
language-adapter result is claimed.

The Go fork pin changes require rebuilding affected Go prefixes and programs.
The LLVM pin changes require rebuilding hosted Clang/LLD/clangd payloads and the
LLVM development prefix. The cross-compiler/container pin stays unchanged.

## Ordered tasks

- [x] Capture before-code QEMU costs with existing tools, single runs unless a
  close result drives a decision; record matched revisions/configuration and
  commands below. No attended repeated sampling.
- [x] Native APPEND, canonical RAM/npfs/FAT/capture serialization, required-support
  CREATE, SDK helper, completion/export integration and interface docs.
  After this task chronium can append concurrently through native capabilities.
- [x] libc, inheritance, shell and dependent ports in linked PRs; restore tee
  append and remove the obsolete workarounds/debt after qualification.
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

Before-code run: source `e13b8f35` (plan commit `e18516a2` changes docs only),
userland `4060486d`, ports `4f2e8aeb`, fs `f3f22b6e`; builder
`pyxis-llvm23.1.3-3d69e83`. QEMU/KVM q35, host CPU, four vCPUs, 3 GiB RAM,
960×752 VirtIO display, unprofiled. RAM-npfs uses the live sparse pool; disk npfs
uses a disposable 128 MiB pool with 8 MiB journal over VirtIO block. Neither
timed path includes SYNC: these measure cached/native mutation costs, not media
durability. Host load at boot was 0.75/1.09/2.69; nested timing is not native.

| Workload | RAM-npfs before | Disk npfs before |
| --- | ---: | ---: |
| iobench 1 MiB, 4,080-byte WRITE chunks (258 calls) | 6.905 ms; 144.824 MiB/s | 6.983 ms; 143.207 MiB/s |
| iobench 1 MiB, 64-byte WRITE chunks (16,384 calls) | 305.390 ms; 3.274 MiB/s | 338.981 ms; 2.950 MiB/s |
| Lua stdio `w`, 8,192 × 64-byte line + explicit flush | 166.154 ms | 158.862 ms |
| Lua stdio `a`, same loop (current SIZE+WRITE) | 304.126 ms | 318.062 ms |

Single timed sample per cell, not a median/range estimate. iobench performs its
one built-in warmup first; Lua has no warmup. All outputs were verified; no short
writes occurred in iobench. The Lua clock surrounds the write/flush loop, excludes
open/close and verification, and uses `pyxis.monotonic_ns()`; each line is 63 `L`
bytes plus newline. This append baseline measures the emulation being replaced,
not atomic append. Existing iobench checks its generated 1 MiB fixture; Lua checks
the complete repeated-line contents after closing.

Commands, with a fresh output name for each invocation:

```text
boot://iobench.pxe write ROOT/write-4080.bin --buffer 4080 --rounds 1
boot://iobench.pxe write ROOT/write-64.bin --buffer 64 --rounds 1
boot://append-lua.pxb boot://append-bench.lua ROOT/ w
boot://append-lua.pxb boot://append-bench.lua ROOT/ a
```

`ROOT/` stands for `ramlog://` or `disklog://`; the helper script is only the Lua
loop above, using existing interpreter/native clock APIs. The interpreter was
rebuilt from the current pinned recipe against the fresh SDK; unchanged iobench
and bootstrap programs use the same userland pin from the earlier qualified
public archive. Raw commands/output, source/configuration and image provenance
are in `/shared/present/atomic-append/baseline/`. After runs must use the same
profile, scratch layout and workloads. Native validation later is one quick
hands-free run; no native append result is claimed here.

### Native operation / SDK slice

QEMU 4-CPU checks passed on RAM, RAM-npfs and disk npfs: zero support probes,
positioned overwrite and resize followed by append, four independent opens
appending 128 tagged 32-byte records each, complete content/uniqueness checks,
npfs hard-link aliases, READ-only refusal, CUT, native APPEND completion and
explicit SYNC. A separate 1-CPU guest passed the same FAT32 checks on a disposable
64 MiB volume. FAT write authority came from a boot-declared selected source and
an explicit bundle declaration, not a writable ambient root or test API.

The 1-CPU guest also passed capture APPEND, zero probe followed by value handoff,
and HOST refusal before create/truncate. Host-side checks proved the absent names
stayed absent and the existing target retained its contents; old plain `>`
compatibility still passed. This does not implement the new plain-output policy
or migrate libc's append writes. No hardware run, injected short-write/I/O failure
or post-exit heap-leak proof is claimed.
The final capture check also passed a zero probe after value handoff: WRONG_TYPE
with the handed-out value preserved, rather than an invented zero EOF.

Existing iobench now has opt-in `write --append` using native APPEND and verifying
the returned placement. It rejects `--prepared` with append before effects.
Ordinary WRITE controls and native APPEND were measured in separate fresh 4-CPU
boots, with the same scratch pool, output names/order, flags and one timed sample
after iobench's built-in warmup:

| 1 MiB workload | RAM-npfs WRITE / APPEND | Disk npfs WRITE / APPEND |
| --- | ---: | ---: |
| 4,080-byte calls (258) | 2.397 / 5.554 ms | 1.902 / 1.924 ms |
| 64-byte calls (16,384) | 300.653 / 299.741 ms | 303.135 / 305.059 ms |

Small-call native APPEND cost is approximately the ordinary WRITE cost in these
samples. The short RAM 4,080-byte sample is inconclusive: an earlier expanded
cohort measured WRITE/APPEND at 5.542/3.253 ms, reversing the direction. Do not
infer a speedup/regression from these single short intervals. The expanded
cohort also retained four rather than two 1 MiB output files per backing, reaching
npfs's 4 MiB payload-cache bound; it is not the matched performance result above.
There is no additional worker round trip or per-call SIZE in native APPEND.

The earlier stdio log-style baseline remains the before value for the libc slice:
stdio still uses SIZE/WRITE in this delivery, so its later improvement is not
claimed here. Raw records/scripts stay under
`/shared/present/atomic-append/task1/`, including the excluded expanded cohort.
The source rebase onto `b4b54f6d`/userland `3b3dfb0` changes none of these I/O
implementation paths. No new compiler container is needed. Native CREATE's
requirements occupy existing zero-initialized payload padding; the 56-byte
payload and 72-byte message extents are unchanged. The pinned Go `child()` raw
caller already zeros this tail. APPEND callers need the new kernel/SDK, and
language append adapters remain in their pending task.

HOST's accepted refusal and revisit point are tracked in
[debt #1506](https://git.internal/PyxisOS/pyxis-os/issues/1506).

### Libc/shell slice

The same QEMU profile and commands as the before run, rebased onto main
`297e48a4`, userland `79af144` and ports `24e7d6a`, gave these single samples.
The new directory is held and all outputs were checked. No SYNC is timed.

| Workload | RAM-npfs before / after | Disk npfs before / after |
| --- | ---: | ---: |
| iobench 1 MiB, 4,080-byte WRITE | 6.905 / 8.922 ms | 6.983 / 2.521 ms |
| iobench 1 MiB, 64-byte WRITE | 305.390 / 319.368 ms | 338.981 / 321.084 ms |
| Lua `w`, 8,192 × 64-byte line + flush | 166.154 / 173.276 ms | 158.862 / 73.357 ms |
| Lua `a`, same loop | 304.126 / 176.525 ms | 318.062 / 180.099 ms |

The retained before binary and rebuilt binary were then paired on the **same new
kernel and fresh scratch pools**, one sample each, because main changed overnight
and the ordinary WRITE control varied. This isolates the client append change:

| Same-kernel Lua loop | RAM-npfs before / after | Disk npfs before / after |
| --- | ---: | ---: |
| `a`, native EOF placement replacing SIZE+WRITE | 300.520 / 171.352 ms | 306.926 / 168.757 ms |
| `w`, ordinary WRITE control | 89.270 / 172.016 ms | 103.520 / 156.532 ms |

Append improves about 43–45% in that pair. The apparent ordinary-write slowdown
was checked once with reversed execution order and fresh names: RAM before/after
170.757/172.085 ms; disk 173.986/156.965 ms. It did not reproduce. The ordinary
WRITE path still submits one WRITE, whereas append removes its former per-chunk
SIZE call. Short intervals and scheduling/cache variation prevent a stronger
throughput claim from these samples; they are not native performance numbers.
Both Lua binaries are 5.5.1 with the same compiler/profile; open/close and content
verification remain outside the timed loop. Raw build, commands, hashes and
qualification records are under `/shared/present/atomic-append/task2/`.

## You should know

Atomicity is per bounded native operation, not per `printf` or `fwrite`, and it
does not add durability or append-only security. HOST append will refuse;
ordinary HOST `>` remains supported through a PIPE-backed positioned relay.
That fallback needs the existing declared helper grants and is not seekable.
Language adapter and native-hardware delivery remain pending.
