# Kernel log

Caelum retains output in a static 256 KiB ring from its first byte. Newline
delimits a line, independently of logging calls. An unfinished line is readable
immediately. Retention continues after the Caelum tab takes over. The first TTY
selection replays retained lines once, without changing the framebuffer drawing
or presenter handoff.

## Levels

Every record has one of five levels, from most to least severe: error, warn,
info, debug and trace (`enum log_level` in [the log ABI](../../include/abi/log.h)).
Kernel code logs with `kerror`, `kwarn`, `kinfo`, `kdebug` and `ktrace`; `klog`
takes a level at run time. A line's level is the level of the record that
started it. It is stored in the ring beside the line's length; message text is
free-form and never parsed for a level.

`LOG_LEVEL` sets the build ceiling: `info`, `debug` (the default) or `trace`.
Records deeper than the ceiling are not compiled, and their arguments are not
evaluated. Error, warn and info are always compiled.

The `log.level` boot option sets the console threshold: `error`, `warn`, `info`
(the default), `debug` or `trace`. An invalid value warns and uses info; a level
deeper than the ceiling warns and uses the ceiling. It is fixed for the boot.
Image builds pass it with `LOG_CONSOLE_LEVEL`.

| Destination | Records |
| --- | --- |
| Ring, so `log` readers and UDP capture | every compiled record |
| Serial, early console, Caelum tab | at or above the console threshold |

The Caelum tab draws errors in red, warnings in yellow and debug or trace in
grey, restoring the default colour after each record. The early console is
monochrome. The first TTY selection applies the same threshold and colours to
the replayed lines. Output without a level of its own, serial-mirrored console
writes and the `LOG_PUTCHAR` system call, is info. Boot init's console output is
drawn by its own console path, and mirrored to serial, whatever the threshold.

### Choosing a level

- **Error:** a device, service or operation is now unavailable, stopped or lost
  data, including fatal exception reports.
- **Warn:** an invalid boot option, a refused request, or a recovered or
  degraded path.
- **Info:** state worth seeing on every boot. Absent optional hardware and lines
  enabled by an explicit diagnostic boot option, such as `usb.xhci.diagnose`,
  stay info.
- **Debug:** one-off detail: inventories, capability lines and state changes.
- **Trace:** per-operation detail, such as HD Audio verbs and stream starts,
  VirtIO GPU commands, Bluetooth boot command traffic, ACPI notifications and
  clipboard operations.

Messages keep their subsystem prefix and wording; existing text such as
`ACPI: error:` predates levels. Per-subsystem thresholds are not implemented;
they come only if the global ones prove too coarse. The
[cost record](../development/experiments/log-levels-cost.md) measures each
ceiling.

## Retention

Console output marked for serial mirroring is also retained, including trusted
boot-init messages about configuration, missing volumes and space startup.
It takes the log lock and then the terminal's output lock, so its ordering with
kernel lines matches serial output. Ordinary local and remote terminal output is not
serial-mirrored and does not feed back into the ring.

Each retained line costs six bytes of length, level and source bookkeeping within the
ring. Overflow evicts whole oldest lines and counts them. A single line longer
than 262139 bytes, including its newline, cannot fit: its retained prefix is evicted
and its remaining bytes are discarded through the newline. This counts as one
lost line. The ring needs no heap, clock, network or initialized CPU-local state.
It is volatile and does not survive reboot.


## System log writes

Spaces, services and programs write to the system log instead of an interactive
terminal. Decisions accepted by chronium on 2026-10-29; the first slice implements
the rows marked done.

- **Authority.** `LOG_RIGHT_WRITE` on the `log` resource, beside READ. Boot init
  holds it, forwards it to each space's init, and a session handoff keeps it for
  the shell. Programs a shell launches inherit READ only. Nothing else writes,
  except the allocator's `LOG_PUTCHAR`.
- **`LOG_WRITE`.** One call is one line: a level and up to 255 text bytes. Control
  bytes other than tab are logged as `?`. The kernel stamps the caller's space, so
  the line reads `NAME: text`, the space's fixed name and `: `. The ring header
  carries the space's index (a record's `source`; 0 is the kernel), so readers
  filter without a name table. `libpyxis` has `log_write`.
- **Streams.** `LOG_STREAM` returns a write-only console-protocol handle for a
  level. Writes are assembled into lines of at most 255 bytes, ended by `\n` or by
  that length; `\r` is dropped, control bytes become `?`, an unfinished line is
  logged when the last handle closes. `isatty` is true and SIZE and READ fail.
  `libpyxis` has `log_open_stream`.
- **Services.** `service start` and `service replace` bind a service's stdout to
  an info stream and its stderr to a warn stream, as the shell's space.
  `--terminal` keeps the space console. A shell without the write right keeps the
  console too, as before.
- **Rate.** A space may log 1000 lines per second with a burst of 5000. A line
  beyond that is dropped and counted; the next line logged reports
  `log: dropped NAME N log lines` at warn, at most once a second. The bucket is
  per space and kept under the log lock. The rate is a starting point to be tuned
  from real use.
- **Display.** Lines show on the Caelum pane at or above `log.level`, coloured by
  level, with the prefix. Lines written by spaces are not mirrored to serial or the
  early console: serial costs milliseconds per line while the log lock is held. They
  stay in the ring, so `log` and UDP capture have them. `log -s NAME` shows only
  one space's lines. The UDP wire's spare header byte carries the source index.
- **Background jobs.** `&` jobs stay on the terminal; an opt-in redirect to the
  log is a later slice.
- **`LOG_PUTCHAR` and `system_log()`.** The system call takes one character of the
  caller's space's info text. Lines are assembled per space (a newline or 255
  bytes ends one; `\r` is dropped, control bytes become `?`) and logged like
  `LOG_WRITE`, so the DHCP line reads `development: eth0: DHCP ...`, is rate
  limited, and no longer goes to serial. Two processes of one space writing at
  once can interleave in a line. It stays an ambient call for programs with no
  handle, such as the allocator's diagnostics. After fatal output starts it writes
  unattributed as before.
- **Cost.** A written line costs about 0.9 µs at a hidden level and 2.7 µs at a
  shown one, against 1.5-1.8 µs for a framebuffer terminal write, and 8 µs for an 81-call
  `LOG_PUTCHAR` line (was 2.3 ms); see
  [the cost record](../development/experiments/system-log-cost.md).

## Read authority and cursors

The explicitly delegated `log` resource implements
[the native log ABI](../../include/abi/log.h). `LOG_RIGHT_READ` permits SNAPSHOT
and READ, with no clear, write or configuration operation. Trusted boot init
forwards it to every configured space, and sessions forward it to their shells,
including remote shells. Ordinary and nested shell launches inherit READ.
Provider launches retain their existing restricted service grants.
The `log` executable stays in the installed rescue archive, so
`boot://log.pxe` also works when the pool and its program revision are unavailable.

SNAPSHOT returns the first retained cursor, current end cursor, cumulative
dropped-line count and ring capacity. A cursor contains a boot-local line
sequence and byte offset, allowing long or unfinished lines to span reads.
READ starts at `{0, 0}` or a previously returned cursor. It returns at most
1024 bytes of records, a next cursor and the number of lines missed by this
reader. Each record is a four-byte header with its length, level and source, followed
by the text of one piece of one line. A piece without a final newline ends the
reply; the line continues in the next read under the same level.
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

Libpyxis supplies `log_get_snapshot`, `log_read` and `log_next_record` through
`<log.h>`; `log_read` validates every record. `log` prints through one captured
end and exits, even while new output arrives. It starts each line with its
level letter (`E`, `W`, `I`, `D`, `T`), and `log -l LEVEL` prints only that
level and more severe ones, and `log -s NAME` only the lines one space wrote.
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
the log lock and scheduler state. If an interrupted writer or reader
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
QEMU capture, with a network configuration whose `management.log` names that network's interface.

The sole BSP network worker follows the retained ring after the `management.log`
interface's driver activates and it has an IPv4 address, assigned statically or by DHCP.
Until then it leaves the read cursor and packet sequence untouched; retained
history is delayed rather than sent during early link-up. Clearing the address
pauses normal sending, including queue-full retries, until an address returns.
The fixed ring's usual overwrite limits still apply while waiting. DHCP
completion demonstrates that the path to its server forwards; a static address
does not establish that forwarding is ready or guarantee host delivery.
It uses Ethernet and IPv4 limited broadcast
`255.255.255.255:2325`, with TTL 1, source port 2325 and the current IPv4 source
address. Fatal output sends immediately after driver activation, including with
source zero while unassigned. There is no ARP, route lookup, lwIP or
userspace forwarder. No interface is selected or initialized for logging.
When caught up, the worker checks every 100 ms; ring overwrite and UDP loss are
possible. Each datagram carries one line fragment of at most 1024 text bytes.
Unfinished text is sent without waiting for a newline. Queue-full retries
retain their packet sequence and text cursor.

[The shared wire header](../../include/remote/log.h) precedes raw text. It
identifies the format, MAC, sampled boot stamp, packet sequence, line, byte
offset and the line's level. A fatal line takes the level of the record that
started it; the panic message is error. All integer fields use big endian byte encoding. Fatal text has a
separate cursor, marked by its flag; the packet sequence spans both streams.
The stamp is sampled during boot and is only a grouping hint. These fields do
not authenticate a sender. Enabled logs are visible across the trusted LAN.
IPv4 permits the zero UDP checksum used here; the IP header is checksummed.

`pyxis-log` prints text immediately to stdout, starting each line with its level
letter and escaping control bytes other than newline and tab. `--level LEVEL`
prints only that level and more severe ones. It reports stream identity, packet gaps, fragment
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
receiver started late can still lose text. The completed
[remote-debugging reference](../development/remote-debugging.md#qualification)
records QEMU and owner-run ThinkPad boot, panic and disabled qualification.
