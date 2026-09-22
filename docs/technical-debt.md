# Technical debt and implementation tradeoffs

Record concrete limitations of implemented choices here: what we chose, its
cost, and when to reconsider it. This is a working record, not a roadmap or a
commitment to replace every simple implementation. Remove or update entries
when the underlying tradeoff changes.

## Contiguous RAM-file backing

RAM files currently own one kernel heap buffer. Growth reserves geometric spare
capacity when possible, falling back to the required size if that allocation
fails. Replacing a buffer temporarily holds both the old and new allocations
and copies the live contents. Large files therefore amplify peak memory usage
and copy cost; one allocation is also subject to TLSF's block-size limit.

Shrinking to a nonzero size retains capacity for reuse. Truncated bytes cannot
be observed after regrowth, but a small file may keep a much larger allocation.
Resize to zero or final object destruction returns the buffer to the heap;
the heap's existing pools remain mapped.

Reconsider this when larger files or memory pressure make those costs material.
Chunked backing and a policy for releasing excess capacity are possible changes,
not requirements for the current milestone. All-or-nothing writes are an
intentional [file contract](processes.md#implemented-file-calls), not debt.

## Retained userspace heap pools

Libc's TLSF allocator reuses freed blocks but retains every backing pool until
process exit. Pools are at least 64 KiB; there is no fixed pool-count registry.
A short-lived peak therefore leaves memory mapped for the rest of that process.
Shrinking `realloc` also keeps the original block capacity; growth may briefly
hold both blocks and copy contents to preserve 16-byte alignment.

Kernel process destruction reclaims all private backing, including live malloc
allocations. Reconsider empty-pool release and in-place aligned growth when
long-lived applications make retained capacity or copying material. The current
allocator and errno assume one thread per process; add synchronization and
thread-local errno when introducing userspace threads.
