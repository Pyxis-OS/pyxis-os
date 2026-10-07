# Kernel log

Caelum retains output in a static 256 KiB ring from its first byte. Newline
delimits a line, independently of `klog` calls; enabled `ktrace` calls use the
same path. An unfinished line is readable immediately. Retention continues
after the Caelum tab takes over. The first TTY selection replays retained text
once, without changing the framebuffer drawing or presenter handoff.

Console output marked for serial mirroring is also retained, including trusted
boot-init messages about configuration, missing volumes and space startup.
It uses the existing presentation lock, so its ordering with kernel lines
matches serial output. Ordinary local and remote terminal output is not
serial-mirrored and does not feed back into the ring.

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
1024 text bytes, a next cursor and the number of lines missed by this reader.
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
that a halted machine can serve userspace readers. Opt-in UDP capture also
receives fatal characters directly, before serial and framebuffer output,
independently of whether the ring can retain them.

The ring store is exactly 262144 bytes. Its object file reserves 262208 bytes
of BSS including synchronization, counters and alignment. It replaces the old
32768-byte early-log store. Each read uses at most 1056 bytes of reply storage
on the calling task's kernel stack. Capabilities use ordinary object storage;
there is no allocation per retained line.

## UDP capture

Build the host receiver and enable kernel broadcast logging:

```sh
make -C tools log
make image LOG_UDP=1
build/tools/pyxis-log > caelum.log
```

An installed or PXE boot entry can instead append `log.udp=1` to its kernel
command line. Omit the option to disable it; the value `1` is the only accepted
value and the option may occur once. It does not depend on `remote.beacon`.
The receiver binds `0.0.0.0:2325` by default; `--bind IPv4` selects a numeric
local address and `--source MAC` filters the advertised MAC. Permit inbound UDP
2325 on the host. Leave the receiver running and boot Pyxis from another
terminal or machine; start it before boot to receive retained history.
IPv4 limited broadcast stays on the local network; QEMU's user-mode NAT does
not deliver this traffic to the host receiver. Use a TAP/bridge network for
QEMU capture, with the normal net0 configuration for that network.

The sole BSP network worker follows the retained ring after the selected net0
driver activates. It uses Ethernet and IPv4 limited broadcast
`255.255.255.255:2325`, with TTL 1, source port 2325 and the current IPv4 source
address, or zero while unassigned. There is no ARP, route lookup, lwIP or
userspace forwarder. No interface is selected or initialized for logging.
When caught up, the worker checks every 100 ms; ring overwrite and UDP loss are
possible. Each datagram carries one line fragment of at most 1024 text bytes.
Unfinished text is sent without waiting for a newline. Queue-full retries
retain their packet sequence and text cursor.

[The shared wire header](../../include/remote/log.h) precedes raw text. It
identifies the format, MAC, sampled boot stamp, packet sequence, line and byte
offset. All integer fields use big endian byte encoding. Fatal text has a
separate cursor, marked by its flag; the packet sequence spans both streams.
The stamp is sampled during boot and is only a grouping hint. These fields do
not authenticate a sender. Enabled logs are visible across the trusted LAN.
IPv4 permits the zero UDP checksum used here; the IP header is checksummed.

`pyxis-log` prints text immediately to stdout, escaping control bytes other than
newline and tab. It reports stream identity, packet gaps, fragment
discontinuities and fatal packet metadata to stderr. Previously unseen late
packets print in arrival order. A 256-packet window suppresses duplicates;
older packets may repeat. It tracks up to 32 MACs, evicting the least recently
seen state when full, and resets a MAC's state when its boot stamp changes.
There is no retransmission, acknowledgement or reordering buffer.

## Fatal network ownership

With UDP logging enabled, the first panicking CPU irrevocably takes the selected
NIC's TX path. Normal networking ends until reboot. Other panicking CPUs do not
share its staging or DMA buffers. Repeated fatal entry by the owner preserves
the stream, as exception reporting may enter before `panic()`; a recursive fault
inside fatal framing or transmission stops further UDP attempts.

Driver hardware mutations publish their operation and BSP identity together in
a short section with interrupts disabled. The gate spans no logging, protocol
callback, allocation, worker notification or sleep. An AP panic closes it and
polls up to 100000 times for the BSP section to finish. Same-CPU interruption
cannot resume; each driver recovers only publication windows it understands.
Failed handoff, interrupted activation/reset or unusable hardware abandons UDP.
An early panic before driver activation has no network path.

VirtIO reserves descriptor 15 and its existing 2048-byte buffer. Enabled normal
TX capacity is 15 of 16; disabled capacity remains 16. Fatal publication uses
the DMA-published available index, covering interruption between the software
producer update and publication. A private used cursor validates completion
batches before reusing the reserved buffer. RTL8111 keeps two slots free in its
normal-priority TX ring, reducing enabled normal capacity to 30 of 32. It records
the exact submitting slot before mutation. If interrupted with OWN clear, it
publishes the first fatal datagram there and in the following slot, covering
both an unpublished frame and an already completed frame. The host suppresses
that possible duplicate. This does not rely on an unqualified priority ring.

Fatal framing has separate static storage and uses no heap, scheduler, clock,
lwIP or held-lock acquisition. Each TX wait polls at most 1000000 times; RTL
also repeats its doorbell within that budget. A timeout or invalid descriptor
permanently ends UDP attempts without reusing device-owned storage. All rings
and mappings remain until reboot. Checked TX completion confirms ownership
return, not host delivery. Carrier loss, stalled hardware, packet loss and a
receiver started late can still lose text. Native ThinkPad panic qualification
is tracked in [remote-debugging task 3](../wip/remote-debugging.md).
