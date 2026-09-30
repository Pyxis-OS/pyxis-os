# Libc input read-ahead

Status: tasks 1 and 2 complete; validation and closure (task 3) remain.

## Completion point

Buffered stdio reads from files and pipes fetch input in blocks instead of one
native read per byte, while ordinary application I/O behavior is preserved.
The recorded [unbuffered line input](../technical-debt.md#unbuffered-line-input)
workload (uniq, 36,009 bytes over virtio-fs, 20.4 s) is rerun under comparable
conditions. The fgets sticky-error bug is fixed in the same milestone (task 1).

This is input only. Output buffering, `setvbuf`/`setbuf`, `ungetc`, threads,
new kernel APIs and unrelated libc expansion are outside scope. Libc targets
ISO C; see [libc portability](../userland/libc-portability.md). Where this
contract defines behavior that ISO C leaves undefined, it is labelled as a Pyxis
guarantee, not portable behavior.

## Current behavior

Confirmed from userland `3b9ba3f` and the interface documents:

- Each descriptor entry owns its native handle and a process-local cursor.
  File reads are positional; there is no shared kernel seek position. A child
  adopting a file-backed standard stream starts at offset 0.
- Pipe reads return available bytes, possibly short, and block only while empty.
  Console input is shared cooperatively: a parent stops reading while its child
  uses the same console.
- Public `read()` and the associated FILE use the same entry. FILE holds only
  indicators and a descriptor number.
- `fgetc` performs one backend transfer per byte; `fgets` and `getline` loop
  over it. `fread_some` performs one backend transfer.
- The shell reads commands through libterm's `input` resource and scripts with
  positional file reads, never through the stdin FILE. It delegates its startup
  stream handles, pipes or fresh redirect handles to children.
- Head's line mode reads one byte per `fread_some` call so it never consumes past
  the newline. Cat, cksum, tee and sha256sum's hashing use `fread_some` or
  `read()` in BUFSIZ blocks.
- `libc_enter` runs `stdio_init`, then `malloc_init`, then `main`.

## Agreed contract

**Ownership.** Read-ahead belongs to the descriptor entry, beside the handle and
cursor it already owns. The entry's position is the logical position: the next
byte the application receives. Fetched but unconsumed bytes follow it; for files
the next backend read starts after them. FILE keeps its indicators and the ISO
stream semantics. Because the entry owns the bytes, `read()` on a descriptor
whose FILE has read ahead returns those bytes first; mixing the two loses nothing.
`read`/`open`/`close` are the portability layer, not ISO C, so this is a Pyxis
guarantee.

**Triggers.** Only buffered stdio reads may read ahead: `fgetc`, `getc`,
`getchar`, `fgets`, `getline` and `fread`. `read()` and `fread_some` return
buffered bytes if any exist; otherwise they perform exactly one backend transfer
of the requested size, as today. Head's exact consumption and cat's prompt
forwarding are unchanged.

**Backend policy.** Files and pipes read ahead. Consoles never do: they are the
interactive device of ISO C (C11 7.21.3p7, C23 7.23.3p7), and read-ahead would
take input typed for the parent shell. Console line input remains one native
read per byte.

**Child delegation.** Buffered bytes are private process memory and never
accompany a delegated capability. A child given the same pipe sees only bytes
not yet fetched; a child given a file-backed stream starts at offset 0, as today.
A program that delegates its stdin must not have read it through buffered stdio;
`read()` and `fread_some` never read ahead. No current launcher reads its stdin
through FILE. This is a native delegation rule, not Unix inheritance.

**Position and state.** Read-ahead is discarded before close only where it can
be refetched: a file keeps its logical position, so dropping its buffer loses no
input. Pipe read-ahead is never discarded except by close.

- `fseek` first validates the request and computes its destination: the entry
  must be a file, the origin valid and the result in range, with `SEEK_CUR`
  relative to the logical position and `SEEK_END` using the current file size.
  Only on success does it discard buffered bytes, set the new position and clear
  EOF. A failed seek, including ESPIPE on a pipe, leaves buffered bytes, the
  logical position and both indicators unchanged. `ftell` reports the logical
  position.
- A write through the entry of an update stream (`r+`, `w+`, `a+`) first
  discards buffered bytes, then writes at the logical position or at the end for
  append. No intervening `fseek` is required; ISO C leaves omitting it undefined,
  and this is a Pyxis guarantee.
- Input `fflush` on a file discards buffered bytes so the next read refetches at
  the logical position. On a pipe it keeps them and returns 0. `fflush(NULL)`
  does not touch input. ISO C leaves input `fflush` undefined; this is a Pyxis
  guarantee.
- A fill happens only while the buffer is empty, so a zero-byte backend read
  (EOF) or a backend error reaches the request that caused it, after all
  buffered bytes. EOF remains sticky and an error does not prevent retries.
- `close`, `fclose` and process exit discard and free buffered bytes. Unread
  pipe bytes are lost, as when a process exits today.

**Transfers.** `fread` keeps its element-count and loop semantics. Each step
consumes buffered bytes first; with an empty buffer and at least one buffer of
the request remaining, it reads directly into the caller's memory, otherwise it
fills the buffer with one backend transfer. `fread_some` keeps its
available-progress contract: buffered bytes are returned without a backend call.

**Allocation.** BUFSIZ (8192) bytes per entry, allocated on the first buffered
read of a file or pipe. Standard entries start without a buffer; reads occur
only after `malloc_init`, so startup does not allocate. If allocation fails, the
descriptor stays unbuffered for its lifetime without an error or indicator.
Descriptor-table growth copies the buffer pointer with its entry.

**fgets.** Decide the current call's outcome with `feof`, as `getline` does: at
EOF, return the partial line if any byte was read; after a read error in this
call, return NULL. A sticky error from an earlier call no longer turns a
successfully read final line into NULL (C11 7.21.7.2, C23 7.23.7.2).

## Accepted limitations

- Buffered file bytes can be stale if another descriptor or process writes the
  same file. `fseek`, `rewind` and input `fflush` refresh them.
- Console input stays one native read per byte, including large pastes into a
  console-stdin program.
- `setvbuf`, `setbuf` and `ungetc` remain absent ISO C functions. A later stdio
  completeness task can build them on this buffer together with output
  buffering.

## Focused tasks

1. [x] **Fix fgets sticky-error handling.** A focused libc change, its stdio
   documentation and the technical-debt entry. Validated in QEMU/KVM with four
   CPUs through the remote terminal, using an uncommitted program compiled by
   guest TCC: after an `fread` element-count overflow set only the error
   indicator, the previous libc returned `first\n` then NULL for `first\nlast`;
   the fixed libc also returned `last`, keeping the indicator set. Terminated
   lines and an empty file were unchanged. A read error during the call still
   returns NULL by source inspection; no consumer injects one.
2. [x] **Add descriptor read-ahead and integrate stdio.** Entry buffer and backend
   policy; separate buffered and exact read paths; discard on write, seek and
   close; input `fflush`; the direct `fread` path. Document the contract beside
   `descriptor.h` and `stdio.h`, and update [stdio](../userland/stdio.md),
   [libc portability](../userland/libc-portability.md) and
   [pipes](../interfaces/pipes.md). Smoke check in QEMU/KVM with four CPUs through
   the remote terminal: the uniq 36,009-byte `host://` workload finished in
   0.35 s with output identical to the host build; a uniq pipeline, sha256sum
   `-c` from a file and a pipe, head on a pipe and the fgets case still worked.
   Formal validation is task 3.
3. [ ] **Validate and close the milestone.** Build the ordinary image and use the
   remote terminal, recording QEMU CPU count and accelerator:
   - Rerun the uniq 36,009-byte `host://` workload and compare with 20.4 s; uniq
     through a pipeline and a redirect.
   - sha256sum `-c` with manifests from a file, stdin and a pipe; stdin hashing
     through `read()`.
   - Head line mode on a pipe remains exact, and its producer still receives EPIPE.
   - Debugger check: with read-ahead buffered on a pipe-backed stream, a failed
     `fseek` returns ESPIPE and leaves the buffered bytes, logical position and
     indicators unchanged, and later reads return those bytes.
   - Cat, the `hello` `w+`/seek path, TCC compile/run, timezone loading, and
     Kilo open/edit/save.
   - A child reading console stdin, then the shell's next command, without lost
     typed-ahead input.

   Update the technical-debt entries, then replace this document with implemented
   behavior in the stdio reference.

No tests, test infrastructure or new utility are included.
