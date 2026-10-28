# Native atomic append

C `a`/`a+` and `O_APPEND`, shell `>>`, Rust `OpenOptions::append`, Go append
opens and LLVM `OF_Append` use native EOF placement. Concurrent writers cannot
overwrite each other's confirmed append prefixes. Atomicity is per native
operation, not per application write, formatted line or flush.

## Operation and authority

`FILE_APPEND` requires existing FILE WRITE; it adds neither a separate right nor
a kernel file cursor. EOF selection and mutation share the canonical backing
owner, including independent opens and npfs hard links. WRITE still authorizes
positioned overwrite, resize and sync: it is not append-only authority.
The [FILE protocol](file-providers.md#native-atomic-append) defines the copied
request/reply and SDK validation; [native completions](native-completion.md)
also support APPEND.

One operation carries at most **4,080 bytes**. Nonempty success returns the
selected start offset and a confirmed contiguous prefix of `1..requested` bytes.
Only the known remaining suffix may be submitted as another operation; other
writers may interleave. Errors provide no usable count. A lost or malformed
mutation reply is OUTCOME_UNKNOWN and must not be replayed.

A zero-length operation checks authority and support and returns EOF plus zero
without mutation or selecting capture byte mode. It reserves no later success.
After a value handoff, a capture producer has no byte EOF: the probe returns
WRONG_TYPE without poisoning the captured value. Capture retention budgets,
immutable TOO_LARGE results and mixed byte/value failure rules remain intact.

RAM, npfs (including RAM-backed npfs), writable FAT32 and native capture byte
producers support APPEND. Read-only/sealed objects retain mutation refusals;
FAT32 retains its owned-ESP write gates, and CUT/lineage still applies.
HOST refuses BAD_OPERATION/ENOTSUP because its explicit-offset adapter cannot
prove atomic placement against external host writers. There is no SIZE+WRITE
fallback. HTTP/text append and PIPE/CONSOLE/STREAM/TRANSACTION append remain
unsupported; native append does not invent a provider transaction contract.

## Open and shell output

Required-support CREATE rejects unsupported append before creation or truncation.
Held or inherited FILEs use the zero probe. The held-directory
`DIRECTORY_APPEND_SUPPORT` query requires CREATE and WRITE_FILES, selects no name
and reserves nothing. Actual creation uses the retained parent and
`DIRECTORY_CREATE_REQUIRE_APPEND`; URI spelling does not determine support.

Plain shell `>` truncates once, then passes a real FILE with append policy on
supported backings. Descendants inherit `stdout.append`/`stderr.append` from the
same startup snapshot as the FILE. Only an unsupported backing such as HOST uses
an owned positioned byte relay: `> host://file` works, but children receive a
nonseekable PIPE and declared helper/pipe/launch grants are required before
effects. `>> host://file` refuses. Provider staging keeps its separate contract.
See [shell redirects](../userland/shell.md) and
[append environment entries](environment.md#shell-selection-and-file-append).

Repeated redirects to one initially absent name retain independent authority.
After the earlier creation, the later redirect looks up once through its own
held parent only after matching parent identity and final component. Its live
FILE identity must match the earlier created FILE; replacement, unknown identity,
authority loss or lookup failure is final. No borrowed authority or mutation retry
is substituted. Lexical `2>&1` shares its selected sink; separate redirects retain
their own cursors. Background redirects remain unsupported.

## Libc and language behaviour

Append replies update the descriptor cursor; reads, seek, shared read-ahead,
`dup` aliases and `fdopen` access/ownership retain their ordinary semantics.
Explicit `pwrite` remains positioned even on append descriptors. See
[libc I/O](../userland/stdio.md).

After uncertain native FILE WRITE, APPEND or pwrite, libc permanently latches
EIO on that shared descriptor's data-write side. Aliases share it; `clearerr`
cannot reset it. Reads, metadata and ownership release remain available; a fresh
open is a new intent. Buffered flush, close, normal exit and Rust BufWriter Drop
therefore cannot replay the uncertain mutation. Known errors keep their existing
recovery. This adds no public last-error slot or new kernel status.

Rust append implies write access; read+append is read/write. Append+truncate is
invalid unless exclusive `create_new` takes precedence. Go uses native append for
syscall opens and inherited/runtime/crash FILE output, retaining its allocation-free
runtime output path. Git's billy adapter uses that Go implementation. LLVM
`OF_Append` follows its upstream Unix create-or-open rule without truncation,
including CreateAlways/CreateNew, and `raw_fd_ostream::pwrite` performs real
positioned I/O. Lua/libuv/Neovim use the libc/inherited FILE policy; sbase `tee -a`
is supported.

LLVM AtomicLineLogger still refuses arbitrary-length atomic lines, and staged
VirtualOutput refuses append because it publishes by replacement. BusyBox
`tar -r` remains outside its selected profile. Large formatted writes, log rotation
and archive terminator handling are not made atomic by this operation.

## Durability and qualification limits

APPEND, `fflush` and close do not imply durability. Existing FILE_SYNC and
directory-sync guarantees apply: RAM is volatile, npfs has its committed-journal
guarantee, and FAT32 is not journaled. There is no new format or power-fail-atomic
log record promise.

[Qualification and measurements](../development/experiments/atomic-append.md)
record QEMU backing/alias/concurrency checks and the vela native RAM smoke.
The newer shell implementation and persistent media remain pending native checks:
[shell output #1537](https://git.internal/PyxisOS/pyxis-os/issues/1537) and
[persistent media #1538](https://git.internal/PyxisOS/pyxis-os/issues/1538).
Injected lost-reply/short-I/O failure and post-exit heap reclamation are not
runtime-qualified. The uncertainty latch was source-reviewed, not fault-injected.
[HOST append #1506](https://git.internal/PyxisOS/pyxis-os/issues/1506) needs a
host-side contract before it can be enabled.
