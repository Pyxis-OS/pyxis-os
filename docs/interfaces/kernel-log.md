# Kernel log

Caelum retains output in a static 256 KiB ring from its first byte. Newline
delimits a line, independently of `klog` calls; enabled `ktrace` calls use the
same path. An unfinished line is readable immediately. Retention continues
after the Caelum tab takes over. The first TTY selection replays retained text
once, without changing the framebuffer drawing or presenter handoff.

Each retained line costs four bytes of length bookkeeping within the ring.
Overflow evicts whole oldest lines and counts them. A single line longer than
262140 bytes, including its newline, cannot fit: its retained prefix is evicted
and its remaining bytes are discarded through the newline. This counts as one
lost line. The ring needs no heap, clock, network or initialized CPU-local state.
It is volatile and does not survive reboot.

## Read authority and cursors

The explicitly delegated `log` resource implements
[the native log ABI](../../include/abi/log.h). `LOG_RIGHT_READ` permits SNAPSHOT
and READ, with no clear, write or configuration operation. Trusted boot init
forwards it to every configured space, and sessions forward it to their shells,
including remote shells. Ordinary and nested shell launches inherit READ.
Provider launches retain their existing restricted service grants.

SNAPSHOT returns the first retained cursor, current end cursor, cumulative
dropped-line count and ring capacity. A cursor contains a boot-local line
sequence and byte offset, allowing long or unfinished lines to span reads.
READ starts at `{0, 0}` or a previously returned cursor. It returns at most
4096 text bytes, a next cursor and the number of lines missed by this reader.
Readers consume nothing globally and maintain their own cursors; there is no
per-reader kernel allocation or mutable capability position.

A read's end is either a captured snapshot boundary or
`{UINT64_MAX, UINT64_MAX}` for the current end. If retained text has overtaken a
reader, READ resumes at the oldest retained line. If a fixed snapshot has been
entirely overwritten, READ reports loss and advances directly to that end;
it never substitutes newer text. Loss counts include a partially read line
whose remaining text was evicted. Byte offsets in overwritten lines cannot be
validated. An empty successful reply means caught up to the selected end.
Future positions and invalid retained offsets fail with BAD_REQUEST.

Libpyxis supplies `log_get_snapshot` and `log_read` through `<log.h>`.
`log` prints through one captured end and exits, even while new output arrives.
`log -f` first prints retained history, then drains new text, sleeping 100 ms
when caught up. Lost-line notices go to stderr. Following costs ten idle checks
per second and up to 100 ms polling delay, plus scheduler/output delays.
Ctrl+C uses the existing foreground-group termination path.

## Synchronization and fatal output

The ring has its own lock. Readers copy into bounded kernel storage while
holding it and copy to user memory after releasing it. They never allocate,
log, wait for I/O or touch presentation state under that lock. Locating a cursor
walks retained length headers, bounded by the fixed capacity. Normal writes
preserve existing presentation ordering; this does not change BSP allocation
or VM ownership.

Fatal entry changes ring acquisition to a single try, independently of GS,
the presentation lock and scheduler state. If an interrupted writer or reader
holds the ring lock, fatal bytes skip retention rather than deadlock or mutate
inconsistent metadata. Serial and early panic-console output continue through
their existing paths. Panic ring capture is best effort; it is not a guarantee
that a halted machine can serve userspace readers. UDP panic delivery belongs
to [remote-debugging task 3](../wip/remote-debugging.md).

The ring store is exactly 262144 bytes. Its object file reserves 262208 bytes
of BSS including synchronization, counters and alignment. It replaces the old
32768-byte early-log store. Each read uses at most 4128 bytes of reply storage
on the calling task's kernel stack. Capabilities use ordinary object storage;
there is no allocation per retained line.
