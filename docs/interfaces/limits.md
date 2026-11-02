# Limits

How Pyxis treats fixed sizes and counts. These are chronium's decisions of
2026-10-29 and 2026-10-30, taken after an audit of every fixed cap in the kernel,
userland, `pkg`, the installer, the tools and the ports. A number chosen for
convenience must not decide what a user can do; a limit that remains is a
property of a device, a format or the memory the machine has, and says so.

## Rules

1. **Size from real capacity.** Where a capacity exists (free space on the
   volume, installed RAM, a device or format field, a wire width), a limit is
   derived from it, not from a constant. A constant stays only for a real field
   width, a recursion depth or a sanity bound on a small file read into a fixed
   buffer, and is named as such.
2. **Refuse before side effects.** A refusal that can be known early comes
   before anything is downloaded, extracted, written or confirmed by the user,
   not after.
3. **One status.** A size or count refusal is `CALL_LIMIT`, with a reason. No
   second status (`BAD_REQUEST`, `FILE_TOO_LARGE`, `IO`) stands for the same
   thing, and no new `E2BIG` status is added: libc maps `CALL_LIMIT` to `E2BIG`
   where POSIX callers need it. The launcher work defines the
   [structured reason ABI](abi.md#limit-diagnostics); producers migrate in
   their own work packages, retaining existing text diagnostics meanwhile.
4. **Name the overflow.** The message says what overflowed, by how much and
   where the limit comes from:
   `<what>: <N> exceeds the limit of <M> (<source>)`.
5. **No silent truncation of user data.** A value that does not fit is refused
   or reported as truncated, never cut without a word.

## Consequences already decided

- The launcher limits (startup size, capture budget, image capture, batch
  size, stack) may break the ABI: the 0.0.x policy in the
  [release process](../development/release-process.md#policy) allows it.
- The live image's RAM pool is sized from RAM, not a fixed 2 GiB.
  [Pool and image defaults](#pool-and-image-defaults) keep creation choices
  separate from the geometry of an existing installation.
- Shell history on disk has no cap: each line is appended and only the tail
  is read ([shell](../userland/shell.md)).

## Pool and image defaults

Accepted by chronium on 2026-11-02. A creation default is not an admission limit
for an existing installation, and changing it never resizes existing storage.

- **Live npfs RAM pool: allocator RAM capacity**, rounded down to 4 KiB,
  without a reserve fraction. Stock `ram_npfs_bytes = "auto"` and omission select
  this mode; positive aligned numeric settings remain available. Allocator
  capacity excludes permanent reservations, and is not currently free RAM or
  installed hardware RAM. Logical capacity is sparse: metadata and touched pages
  compete with programs, so writes can return `NO_MEMORY` before logical space
  runs out. The chosen capacity and source are logged once at initialization;
  filesystem information reports pool capacity and free space. There is no
  64 GiB policy ceiling or chosen 8 MiB floor: the real layout must fit its
  headers, bitmap, volume table, journal and initial root. Too-small layouts
  return `LIMIT` before backing allocation. Capacity stays fixed until reboot.
- **Classic ESP: 1 GiB** for a new install, at the existing 1 MiB offset. This
  gives staging and recovery more headroom at another 512 MiB of disk cost.
  Update uses the reviewed existing GPT extents, including older 512 MiB ESPs;
  shared Coexist ESPs are not enlarged. Checked free space must cover retained
  recovery/current payloads plus fresh staging; later cleanup gives no credit.
- **Coexist pool: the whole selected MiB-aligned gap**, editable before Review.
  The largest-gap choice and lower-LBA tie remain. Review shows the selected
  gap, chosen pool and unallocated tail, and warns that the pool cannot currently
  be resized. Reserving space for another use requires an explicit smaller size.
- **Published QEMU disk: 128 GiB virtual, sparse** for future images. A useful
  development disk avoids stranding a trial install at 32 GiB. The retained
  npfs bitmap grows with logical capacity; sparse storage is not free metadata.
  Compressed download, allocated host bytes and virtual size are separate facts.
  Existing published images stay unchanged and remain verifiable. Growing a
  qcow2 alone does not enlarge its GPT partition or npfs pool; actual guest
  writes can eventually fill host storage.

The separate developer USB sample keeps its explicit image/ESP settings. These
defaults do not change partition authority, publication ordering or journal policy.
See the [installer](../userland/installer.md) and
[QEMU image](../development/qemu.md), and the
[disposable qualification record](../development/experiments/pool-sizing-defaults.md).

## Package indexes and fetching

The second package. The package command index (`sets/N.commands`) and the `sdk://`
index (`sets/N.sdk`) are held whole in kernel memory. They were limited to 256
commands and 1 MiB each out of a fixed 4 MiB, which also capped the number of
installed packages with files at about 1,080, and the limits were reported only after
every archive was downloaded. They are now bounded by one thing: the kernel's package-view
memory, one 512th of installed RAM and at least 4 MiB, reported in
`package_command_info.budget`. `pkg` checks the two indexes against it before the review and
names the sizes and the limit; the file format is unchanged, so a set staged by an older
`pkg` stays valid. The size of a set's indexes depends on each package's manifest, which `pkg`
can read only after extraction; a refusal that needs earlier knowledge would put counts in
the repository index, which is not done here.

A fetch has an inactivity window, not a total time derived from its size: it fails after 30 s
without data (`--timeout SECONDS` changes the window and has no upper bound) and a transfer
that keeps arriving never expires. See [package formats](packages.md#command-projection-and-bootstrap)
and [HTTP transactions](http-transactions.md).

## Literal parser

The first package of that work. `userspace/lib/literal.c` limited a document to
8,192 values and entries and 1 MiB, which capped a repository index at 511
ABI-bound versions and an installed set at about 800 packages. It no longer
limits the number of values, entries or comments, finds duplicate keys in linear
time, and bounds work in proportion to the source. A repository index and set
record are read whole and parsed without a source limit; the remaining bounds are
memory, the table depth of 32 (recursion), and a 1 MiB limit on small documents
read into a fixed buffer. The details and the compatibility consequence for older
clients are in [package formats](packages.md#literal-data) and the
[release process](../development/release-process.md).

**Open follow-up (not in this change).** `pkg` parses a repository index before it
verifies the signature, because the parsed header names the signer. With no
parser cap, a hostile or broken server can make the client allocate for an
index of any size. Signature verification over the raw bytes should come before
parsing; the signer would then have to be named outside the signed text (for
example by the signature file) or checked after a first verification against the
pinned CA. This needs a design decision before any code.

## Bundles, tar, clipboard and scrollback

The third package (work package 9).

- **Bundle manifests.** The reader has no limit on the manifest size or on the number of
  commands, resource directories or grants; tables grow with what the manifest declares and
  duplicate keys are found by sorting. A path component stays at 255 bytes (NAME_MAX), the JSON depth at 16
  (recursion). What the launcher can carry in one request is the launch path's bound.
  The host check in `tools/pxpkg.py` applies the same rules.
- **tar.** The BusyBox port writes and reads GNU long-name records, so no name length drops a
  member (see `ports/busybox/TAR.md`).
- **Clipboard.** No constant limits an item. Stored text shares one budget, one 512th of installed
  RAM and at least 8 MiB, reported by `CLIPBOARD_INFO`; a copy that does not fit says so with both
  sizes. A graphics read is size-first (`HAS` reports the length, `READ` must fit).
- **Multiplexer scrollback.** A pane's history is limited by memory, not by a row count: one 512th
  of the allocator's capacity per pane, at least 8 MiB, or `terminal.scrollback_bytes`. The
  history view reports rows dropped and the limit.

The share, one 512th of RAM with an 8 MiB floor, is the one the package views (4 MiB floor) and
the clipboard take.

## Network storage

The fourth package (work package 8, on top of the receive window of #1647).

- **Budget.** TCP and UDP storage share one budget: one sixteenth of installed RAM, at least 64 MiB,
  set when networking starts. It is a bulk-data share: a TCP stream charges about 2 MiB (its
  1,048,560-byte ring and an equal receive reservation), so the small-text share of RAM/512 used by
  the clipboard, package views and scrollback would be below the old 64 MiB on a 16 GiB machine. At
  64 MiB a machine held 31 streams; at 4 GiB it now holds about 127. A refusal is `NO_MEMORY`
  (a state that clears when streams close, not a property of the request) and the kernel log
  reports, at most once a second, how many admissions were refused and the budget's use.
- **Listener backlog.** `LISTEN` carries a `backlog` (at least 1; 0 is `BAD_REQUEST`). More than a
  quarter of the streams the budget holds, or 255 (lwIP's counter width), is clamped, as listen(2) does,
  and the reply's `capacity` is the effective backlog; the kernel logs the first clamp. A half-open
  connection already holds its 2 MiB for ten seconds, so one listener cannot take more than that
  share. This is an ABI change; callers pass their own number.
- **UDP receive queues.** Per endpoint, bytes instead of a datagram count: a 256th of the budget
  (256 KiB at the floor), records charged to the budget and no longer part of the 32-packet
  software budget. Drops are counted per endpoint (`dropped` in `UDP_INSPECT`) and the log reports
  the first.

Not changed here: the send budget (65,535 bytes), the out-of-order buffer (16 segments) and the
32-packet software budget. They need a latency and loss rig that the QEMU user network does not
provide; see [the experiment record](../development/experiments/network-sizing.md).

## Launcher capacity

Chronium accepted WP3.1–WP3.7 on 2026-10-30. Launch metadata and startup mappings
are measured from the request; executable snapshots follow the actual file size.
The 64 KiB capture/startup, 128 MiB serialized-image, 256 MiB virtual-span and
eight-child batch ceilings are removed. Batch replies and kernel, shell, libc
and Lua stage storage are variable-size; scalable waits use existing readiness
sets. Physical allocation failure stays CALL_NO_MEMORY. P1F layout, address
encoding, capability authority and atomic child publication remain enforced.

The default main stack remains 1 MiB with eager backing. Explicit positive
page-aligned stacks are bounded by their actual stack/guard address layout,
without the former 1–8 MiB policy. **Automatic growth is deferred**: runtime
stack exhaustion can still fault, and fault handlers gain no VM authority.
Environment-table, group and readiness admission policies remain separate;
known launcher refusals name that owner and preserve their quantities. This does
not claim the rest of the audit is complete or change those remaining policies.

The reason carrier is a nullable 32-byte numeric record selected in syscall R9;
RAX/RDX retain status/reply-byte meanings. Only CALL_LIMIT writes the record.
This, the 88-byte startup record and variable batch/pipeline results require
coordinated kernel/SDK/program rebuilds, including native LLVM development
libraries and hosted tools. Go's copied adapters and native linker are updated;
Rust consumers relink through SDK/libc. See [program loading](../kernel/program-loading.md),
[batch launch](processes.md#batch-launch), [native jobs](../userland/native-jobs.md)
and the [diagnostic ABI](abi.md#limit-diagnostics) for ownership and error rules.

## The remaining numbers

The fifth package (work package 10).

- **`cp -r`.** No entry count or nesting limit; the level stack grows on the heap.
- **Shell completion.** No limit on command names or directory entries.
- **HTTP request bodies.** A RAM staging upload has no fixed limit: a write is refused with
  `CALL_LIMIT` when it would leave less than an eighth of total memory free (the `system_info`
  report, because the RAM volume is memory shared with every other volume), and a log line names
  the numbers. `--staging-cap` adds a fixed cap; NPFS staging is bounded by its backing.
  A URL longer than 2,048 bytes (a split of the 4,096-byte request packet) is `CALL_LIMIT`.
- **Process listing.** A call returns every record that exists; the kernel sizes its buffer to the
  count it takes just before, and `procs` asks again with room for the reported total.
- **Endpoint exports.** No count limit. Exports share one kernel budget, one 1,024th of RAM and at
  least 4 MiB, and are found through a hash index. The 16 delivery records stay: a backpressure bound.
- **Terminal geometry.** Up to 65,535 columns and rows, the width of the POSIX `winsize` fields.
  The remote terminal protocol keeps its own 512 x 256.
- **Log writes.** Text longer than 255 bytes continues on the next line; a call takes up to 4,096.

## Limits that stay

A number that is not removed above is a device or format field, a protocol width, a safety bound on
what one process can make the kernel allocate, or an upstream default. Each is listed with its source
so that the next audit starts from here. Rules 1 to 5 still apply: where a refusal can be known early
it comes early, and it names what overflowed.

| Limit | Value | Why it stays | Where |
|---|---|---|---|
| Endpoint delivery records | 16 per endpoint | Backpressure bound, `QUEUE_FULL` is retryable; each record embeds two 4 KiB buffers | `include/abi/endpoint.h` |
| Readiness metadata | 16 MiB per process | Bounds kernel memory one process can pin; `INFO` reports ceiling and use (about 65,000 registrations) | `kernel/user/readiness_set.c` |
| Native completion metadata | 16 MiB per process | Same, per process | `kernel/service/completion.c` |
| Execution groups | depth 16, 256 per space | Admission policy, not ABI | `kernel/object/execution_group.c` |
| JIT backing | 256 MiB, 4,096 regions per process | Initial per-process policy; moves with the memory work | `kernel/mm/private.c` |
| File and I/O envelope | 4,096 bytes (reads 4,088, writes 4,080) | ABI envelope; large transfers use bulk paths | `include/abi/file.h` |
| Shell `$(...)` capture | 64 MiB per value, 512 MiB per session | Reported ("too large to retain"); a file is the answer | `kernel/object/retention.c` |
| USB admissions | 32 HID, 4 storage, 32 topology per controller | Lifetime budgets until recycling lands (debt #1293) | `kernel/usb/settings.h` |
| Boot tables | 256 memory regions, 32 KiB EFI map, 64 GiB of RAM, 256 CPUs | Static arrays sized at build time; RAM above 64 GiB is ignored with a warning | `include/kernel/boot.h`, `kernel/mm/pmm.h` |
| GPT, queue depths, redirects | 256 partitions, small device queues, 10 redirects | Format and tuning bounds | installer, drivers, `libhttp` |
| NPFS and FAT32 layout | 64 volumes per pool, 255-byte names, 4 GiB - 1 per FAT32 file | On-disk formats | `fs/` |
| USTAR | 8 GiB - 1 per file, 250-byte paths | The package archive format | `pkg/metadata.h` |
| Wire fields | UDP 1,472, DNS names 255 / labels 63 / messages 512, TLS record 16,384 | Protocol widths | `include/abi/udp.h`, `common/dns.h` |
| Hardware cursor | 64 px | Device plane | `kernel/display/virtio_gpu.c` |
| Ports | LuaLS channel budgets, font and file-list arrays, Quake strings, `less` lines | Upstream defaults and tuning inside a port | `ports/` |
