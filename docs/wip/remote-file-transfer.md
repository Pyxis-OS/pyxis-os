# File transfer through the remote terminal

Status: **accepted, 2026-10-04.** The owner chose the frame format, the
confirmation policy and the scope [below](#owner-decisions). Any decision can be
revised later by the owner. Each task starts only when the owner says so.

**Streaming transfers: implemented, 2026-10-08** (Pyxis #551, userland #165).
See [streaming transfers](#streaming-transfers) and its
[validation](#streaming-validation-2026-10-08).

**Transfer throughput: done, 2026-10-09.** The reply-read fix (#554, userland
#167) and the [network throughput](../development/network-throughput.md) work
brought a native 15 MiB download from about 45 s to 7.5 s; see
[transfer throughput](#transfer-throughput).

## Goal

Move single files between the host running `pyxis-remote` and Pyxis through the
remote terminal session itself:

- **Upload:** drop a file from the host, for example an externally compiled
  `.class` file, into the remote terminal, and it lands in the shell's current
  directory.
- **Download:** copy a Pyxis file to the host, for example to back up work in
  progress in case of filesystem corruption.

This is userland and host-tool work with no kernel change.

A host file server such as dufs or `python3 -m http.server`
also serves uploads: `cat http://HOST:PORT/Foo.class > Foo.class` works today.

## Shape

The transfer travels inside the existing terminal stream, as ZMODEM and kitty's
transfer do. No new frame type, service or authority is added to the
[remote terminal](../userland/remote-terminal.md).

- **On Pyxis:** an ordinary program does the file I/O. It runs with the shell's
  grants and working directory, and exchanges escape-sequence frames over its
  console. The remote terminal server gains no filesystem power; the program
  can only read or write what the shell could.
- **On the host:** `tools/remote` recognizes those frames in OUTPUT before
  presentation, and answers through INPUT. It touches host files only after the
  user confirms, and only in places the user chose.
- **Frames:** base64 data chunks, small enough to fit the 4096-byte frame
  payloads, with a SHA-256 check over the whole file. Existing input flow
  control (the 4 KiB typeahead limit) paces uploads.

## Owner decisions

Accepted 2026-10-04.

1. **Frame format: kitty's [file transfer protocol](https://sw.kovidgoyal.net/kitty/file-transfer-protocol/)**
   (OSC 5113). Implement the subset needed for single regular files: no
   compression, no rsync deltas, no directories or links. A program speaking it
   uses a mandatory negotiated SHA-256 extension with `pyxis-remote` (owner
   revision, 2026-10-06). Peers that do not advertise it are refused. Stock-kitty
   interoperability is outside this implementation because its simple mode has
   no whole-file SHA-256 field or atomic-publication guarantee.
2. **Confirmation policy:**
   - every upload and download is confirmed on the host;
   - an existing Pyxis file is never overwritten without an explicit flag;
   - downloads are saved only into a download directory the user names on the
     `pyxis-remote` command line, never anywhere else; without one, downloads
     are refused;
   - names arriving from Pyxis are reduced to a plain file name, with no path
     components.
3. **Scope: single files only.** Directories and whole projects come from
   porting a small archive tool later, then transferring the archive.

## Task 1 implementation decisions

Agreed 2026-10-06:

- Negotiated SHA-256 is mandatory; unsupported peers are refused. The original
  direct-stock-kitty interoperability expectation is dropped.
- Receivers verify the whole file in memory before writing. They exclusively
  create a recognizable sibling staging file, then atomically rename it into
  place. Pyxis uses NO_REPLACE unless `--overwrite` is present. Handled
  cancellation/errors remove only the staging file owned by that transfer.
- Abrupt process/session death may leave `.NAME.xfer-partial-ID` behind while
  preserving the final target. Stale staging files are never automatically
  removed or overwritten.
- Pyxis uses Mbed TLS PSA for SHA-256. The host vendors one small public-domain
  SHA-256 implementation, with its source pin and local changes recorded.

The explicit-command implementation uses the following contract. The program is named
`xfer`: `xfer send FILE` downloads to the host; `xfer receive [--overwrite]
HOST_PATH NAME` uploads into one plain name in the inherited working directory.
The host option is `--download-dir DIR`; downloads always refuse an existing
host name. Host paths are absolute or relative to the host user's home directory,
as in kitty; no shell expansion is performed.

The OSC 5113 extension is `px_xfer=2`, echoed in the initial `status=OK`; it
replaced `px_sha256=1` when data became windowed; see the
[`xfer` notes](../../userspace/xfer/README.md).
Both sides require that echo before data. The sender includes `sha256=HEX` in
file metadata, and receivers verify that 64-digit digest and the declared size
before publication. Serialized kitty keys (`ac`, `fid`, `n`, `st`, `sz`, `d`)
retain their standard encodings. Uncompressed data chunks are 2048 bytes before
base64 encoding. The first implementation bounded buffered files at 16 MiB;
[streaming](#streaming-transfers) replaced that memory limit without changing
the protocol.

For downloads, `send` also carries name, size and hash for the host's confirmation
before its initial permission reply. Then `file`, windowed `data` with
cumulative `PROGRESS` replies, `end_data`/file `OK`, and `finish`/session `OK`
complete the transfer.
For uploads, `receive` plus one file query precede confirmation; the host replies
with session `OK`, one regular-file metadata frame (`st` is its actual file ID),
and catalog `OK`. The Pyxis program then requests that file and the host supplies
`data`/`end_data`. The same window and cumulative `PROGRESS` replies pace
uploads. After verified
atomic publication, Pyxis sends `finish`; the host acknowledges session `OK`.
Cancellation uses `cancel` and `status=CANCELED`, draining transfer replies before
returning to the shell. The host disconnects if cancellation is not acknowledged
within five seconds, preventing late protocol replies from becoming shell input.
A completed file is published at the atomic rename;
subsequent cancellation cannot undo that completed operation.

## Task 2 implementation decisions

Agreed 2026-10-07:

- The interactive root remote shell opts into an OSC prompt marker in existing
  output; no new terminal wire frame or kernel interface is needed. The host
  invalidates its known empty prompt on ordinary input and restores it only
  after command completion and a new marker. Once input is forwarded while a
  command is pending, uncertainty stays latched until reconnection. This is
  deliberately conservative; input consumption is not acknowledged by the
  current protocol. [Consequences and revisit point](../technical-debt.md#remote-drop-prompt-tracking)
  are recorded separately.
- Detect one absolute or `~/` existing regular-file path in a bracketed paste,
  including shell quoting and backslash escaping. Multiple paths, unsupported
  quoting and ordinary text retain normal paste behavior.
- The drop confirmation authorizes only the matching task-1 upload, avoiding a
  second confirmation. No overwrite flag is injected. Refusal consumes the drop.
- Outside a known empty prompt, including a running editor, the original paste
  is forwarded as ordinary text, without injecting an upload command. This
  resolves task 2's contradictory "never typed" sentence in favor of its stated
  fallback and finish criteria.

## Tasks

- [x] **1. Transfer by explicit command.**
  - **Pyxis program:** a userland program, with its name settled in
    implementation. It sends a named file to the host, or receives a host path
    into a name in the current directory. Its console must pass ESC and Ctrl+C
    through as data while transferring, as Kilo's and vi's raw mode do.
  - **Host client:** `tools/remote` intercepts OSC 5113 in interactive mode,
    shows each confirmation, and verifies the SHA-256 before writing anything.
    Machine mode keeps passing every byte through unchanged.
  - **Failure:** a cancelled, interrupted or mismatched transfer leaves no
    partial target file on either side. A refusal or error is reported in both
    terminals.
  - **Finish when:** in QEMU over the remote terminal:
    - an uploaded `.class` file and a 1 MiB file match the host's SHA-256;
    - a downloaded file lands only in the named download directory and matches;
    - overwrite without the flag is refused;
    - cancelling on either side leaves no partial file;
    - unrelated output and paste still behave exactly as before.

- [ ] **2. Drag and drop.** Implementation is complete in
  [Pyxis PR #455](https://git.internal/PyxisOS/pyxis-os/pulls/455), branch
  `remote/drag-drop`, with [userland PR #137](https://git.internal/PyxisOS/pyxis-userland/pulls/137)
  (`remote/drop-prompt`, published `352cf14`) merged first. Linux client/guest
  checks passed. Actual GUI terminal acceptance remains pending.
  - [x] Bracketed-paste detection, shell signaling and one-confirmation upload.
  - [x] Linux QEMU checks for transfer, refusal and ordinary/editor fallback.
  - [ ] Owner GUI-drop validation from Linux and macOS host terminals.
  - **Detection:** `tools/remote` enables bracketed paste on the host terminal.
    When one paste is exactly the path of one existing regular file, with the
    host terminal's quoting removed, the client offers: "Upload `Foo.class` to
    the current directory? [y/N]".
  - **Upload:** on yes, the client enters the task-1 receive command for that
    path at the shell prompt, and the transfer runs as in task 1.
  - **Fallback:** if the client cannot tell that the shell is idle at a prompt,
    the paste goes through as ordinary text. A dropped path must never inject
    an upload command into a running program such as vi; its original text is
    pasted normally.
  - **Finish when:** dropping a file from a macOS and a Linux host terminal into
    an idle remote shell uploads it after confirmation. Dropping one into a
    running program, or pasting ordinary text, behaves as a normal paste.

## Task 1 validation (2026-10-06)

Ordinary `make -C tools remote` and `make -j16 image PREBUILT='kernel sdk ports'`
passed without new warnings. The userland bundle was rebuilt from source; kernel,
SDK and ports reused independently verified bundles with unchanged inputs. No
compiler-container rebuild is needed.

Interactive nested-KVM QEMU used four CPUs, 512 MiB, virtio-net and virtio-rng,
OVMF and TCP forwarding `2423:2323`; host fixtures/downloads were under `/dev/shm`.
The host was Linux. The Java 8 `.class` fixture was a valid minimal 60-byte class,
and the 1 MiB fixture repeated bytes 0..255. Guest `sha256sum` matched host sums:

- `.class`: `baad6d03e1c39fece4499fdc33a97885f2b75ff8d668fac968f1d555d33692ca`;
- 1 MiB: `fbbab289f7f94b25736c58be46a994c441fd02552cc6022352e3d86d2fab7c83`.

The downloaded 1 MiB file had the same digest and landed only in the chosen
host directory. Guest NO_REPLACE refused an existing name and retained its hash;
explicit `--overwrite` replaced it with an empty file whose digest matched.
Empty-file download also worked. Existing host names were refused after staging,
with original targets preserved and staging cleaned. Downloads without an option,
host confirmation refusal and receiving into read-only `boot://` failed clearly.

Host Ctrl+C canceled uploads and downloads after acceptance, with neither partial
targets nor staging names left in the inspected directories. Guest Ctrl+C through
the raw machine client sent cancellation and consumed a manually supplied peer
acknowledgement; no target was created. Machine output retained the OSC bytes,
including the SHA-256 negotiation. A separate unacknowledged guest cancellation
reported its bounded timeout. Read-only GDB inspection of the waiting `xfer` task
confirmed inherited directory, clock/random and terminal grants, plus its held
passthrough token.

Ordinary output and a multiline paste (read a boot file, copy it to RAM, read the
copy and remove it) produced exactly the same 60,099 host presentation bytes as
the baseline client. Hash-mismatch and unsupported-negotiation refusal were
reviewed in code; corruption/fault injection was not run. macOS host publication
and persistent-backend durability were not measured. QEMU/client/debugger jobs
were stopped after validation.

Review follow-up: an existing guest destination now reports `NAME already exists;
use --overwrite to replace it`, with EEXIST status. That certain refusal no
longer uses the rename-outcome uncertainty wording; other rename failures retain
the existing diagnostic. Both task branches were rebased onto current main after
the system-layout merge, keeping its repository changes in the base. A full
source `make -j16 image` and native-client build passed; transfer compilation had
no warnings (the full ports build still emitted existing vendor warnings).
On the refreshed four-CPU nested-KVM image, refusal printed the new message,
retained the original SHA-256, and left no staging name; explicit `--overwrite`
still published the empty fixture with the matching empty-file digest.

## Task 2 validation (2026-10-07)

`make -C tools remote` passed without warnings. Full source `make -j16 image`
passed with the existing compiler and a locally available CMake added to PATH;
ports still emitted their existing vendor warnings. The marked editor changes
libterm, so SDK and ports were rebuilt as well as userland. No compiler-container
rebuild is needed. Kernel, filesystem, lwIP and ports source pins are unchanged
from parent main `c76bb80`; the userland dependency starts at `56b9c0e`
and is published as `352cf14` in [userland PR #137](https://git.internal/PyxisOS/pyxis-userland/pulls/137).

Interactive QEMU used the patched 10.2.2 emulator, raw OVMF, four CPUs, 512 MiB,
nested KVM, virtio-net/rng and loopback forwarding `2423:2323`. The Linux native
client used a 100x24 PTY. Bracketed-paste input was supplied manually through
that terminal, including an end delimiter split across two writes; no GUI file
drop was performed. No new tests, fault injection or boot/output automation was
added, and no performance result is claimed.

A valid Java 8 `.class` (55 bytes), a backslash-escaped basename containing a
space, and a double-quoted basename containing an apostrophe uploaded with one
confirmation. Their guest and host SHA-256 matched
`5d9ac23bf989e7c9e6d96b878e43faab063ec75ae5b6a057a81562a7c05cd2cf`.
A quoted `~/` path uploaded a 1 MiB file with matching digest
`fbbab289f7f94b25736c58be46a994c441fd02552cc6022352e3d86d2fab7c83`.
After the final host changes, another 1 MiB drop and explicit download matched
that digest in the selected download directory. An existing guest name was
refused with the `--overwrite` diagnostic and retained its original digest.

Host `n` and Escape refused a drop and restored the prompt; the next drop was
offered. Pasting `y` during confirmation did not accept it. Empty Enter/Ctrl+C
preserved subsequent detection. Host Ctrl+C canceled a dropped 16 MiB transfer;
listing `tmp://` showed neither its target nor a staging name.

In vi insert mode, a dropped path appeared as literal text and saved exactly as
that text, without an upload offer or command. After vi exited, the uncertain
connection continued to paste paths normally; reconnecting restored detection.
An ordinary multiline paste ran `ls` and `cat` normally. Default machine mode
preserved OSC 133 bytes in base64 output; quiet machine sessions returned typed
completion and FINAL records without markers. Ctrl+] returned acknowledged closure
and restored terminal settings/bracketed paste. Read-only GDB at a terminal read
observed zero queued input, 359 output bytes and one passthrough holder.
After final client closure, the completed-task queue was empty. All task-owned
QEMU, client and debugger jobs were stopped.

Userland Actions lists zero tasks. `fj pr status 137` fails to parse Forgejo's
empty status value ("unknown variant ``"); no dependency CI pass is claimed.

Actual Linux GUI drops and macOS terminal drops/publication were not measured.
The milestone remains open for owner terminal acceptance. After that passes,
close the task checkbox, turn this WIP document into the implemented file-transfer
reference and update links as specified by AGENTS.md.

Rebase follow-up: main advanced through the libc regex merge during validation.
The task branches were rebased onto Pyxis `ee69871` and userland `ea0f48c`,
resolving the parent gitlink/debt-note conflicts while preserving both features.
A new full source `make -j16 image` passed. On the rebuilt four-CPU nested-KVM
image, `rebased.class` again uploaded with one confirmation and the matching
`.class` digest above. Quiet machine completion, FINAL and acknowledged client
closure still worked. Those client/QEMU jobs were stopped as well. The final
published userland dependency is `352cf14`; the base's other pins are unchanged.

## Streaming transfers

Proposed and accepted 2026-10-08. The owner accepted the three defaults
[below](#streaming-decisions).

### Need

Before streaming, each side buffered the whole file and the limit was 16 MiB.
The owner wants to put retail Diablo data on the installed ThinkPad stick:
`DIABDAT.MPQ` is 517 MB, and Hellfire's MPQs add about 175 MB. Within that
limit they had to be split into about 45 pieces and joined with `cat`.

### Baseline

Measured on 2026-10-08 in QEMU 10.2.2 with nested KVM, 4 CPUs, `VIRTIO_NET=1`
and a forwarded TCP port. The guest image was #539's head (main `abb251f`,
ports `5000ae6`, userland `df78002`); the host's `pyxis-remote` was built from
main `769535a`. Each time runs from pressing `y` at the host's confirmation
until the shell prompt returned. The files were random data in `tmp://`.

| Direction | Size | Times | Rate |
| --- | --- | --- | --- |
| Upload (`xfer receive`) | 1 MiB | 2.12 s | 0.49 MiB/s |
| Upload | 15 MiB | 33.3, 34.1, 33.5 s | about 0.46 MiB/s |
| Download (`xfer send`) | 15 MiB | 81.2, 80.6, 81.1 s | about 0.19 MiB/s |

All downloads matched the source's SHA-256 on the host.

The rate comes from the framing, not from buffering:

- Uploads send 2048-byte chunks, and the host waits for a PROGRESS reply to
  each one, as the [`xfer` notes](../../userspace/xfer/README.md) describe.
  That is about 4.3 ms per chunk.
- Downloads take about 10.5 ms per chunk. Their cost has not been profiled.
- Uploads also pass the remote terminal's 4 KiB typeahead limit, so at most
  about two frames can be outstanding however the protocol changes.

At the QEMU upload rate, the owner's 692 MB would take about 25 minutes. The
per-chunk cost there was measured in nested KVM through a forwarded port.

The owner timed the same 15 MiB transfers natively on 2026-10-08: the ThinkPad
booted by PXE from main `4332801`, with `pyxis-remote` from the same revision
on the desktop host over the wired LAN. These are stopwatch times from
confirmation to the prompt.

| Direction | Size | Time | Rate |
| --- | --- | --- | --- |
| Upload (`xfer receive`) | 15 MiB | about 6 s | about 2.5 MiB/s |
| Download (`xfer send`) | 15 MiB | about 45 s | about 0.33 MiB/s |

Natively, uploads run about five times faster than in QEMU, so the 692 MB
would take about 4½ minutes, which the owner accepts. Downloads stay slow, so
the throughput follow-up after streaming concentrates on them. Streaming lifts
the size limit but does not, by itself, make transfers faster.

### Proposed shape

- **Pyxis side.** `xfer` reads and writes the file in chunks and keeps memory
  constant: one frame, the chunk being encoded or decoded, and the SHA-256
  state, independent of file size.
- **Host side.** `pyxis-remote` does the same.
- **Receiving.** A receiver exclusively creates the existing
  `.NAME.xfer-partial-ID` staging file when data starts, writes each verified
  frame to it, and hashes the bytes as they arrive.
- **Publishing.** Only after the declared size and SHA-256 both match does it
  synchronize the staging file, rename it atomically with today's NO_REPLACE
  or explicit-replacement rules, and synchronize the directory. A mismatch
  publishes nothing.
- **Cancellation and handled errors**, including running out of space, remove
  only that transfer's staging file. Abrupt death leaves it behind, as today,
  except that it can now hold a partial file of any size. It is still never
  removed automatically.
- **Finding orphans.** A leftover staging file sits beside its intended target
  as `.NAME.xfer-partial-ID`: in the Pyxis directory for uploads, in the
  download directory on the host for downloads. Pyxis `ls` lists dot names;
  on the host, `ls` needs `-a` to show them. No transfer reuses or resumes a
  staging file, so one is safe to delete by hand once no transfer into that
  directory is running.
- **Confirmation, overwrite rules, name limits and deadlines** stay as they are.
- **No resume and no progress display.** Both remain out of scope.

### Streaming decisions

Accepted by the owner 2026-10-08, all as the defaults.

1. **Publication contract.**
   - **Default:** the shape above. Unverified bytes go to disk, but only into
     the private staging name, and are published only after verification.
   - **What replaces the 16 MiB cap:** no fixed size limit. A transfer is bound
     by its declared 64-bit size, the destination's free space, and the
     existing per-reply deadlines.
   - **Alternative:** keep verifying in memory, but raise the cap to a bound
     the owner names. Memory would grow with the cap.
2. **Where the SHA-256 is sent.**
   - **Default:** keep today's protocol. The sender hashes the file in one pass
     before announcing it, then reads it again to send. If the file changes in
     between, the sender's second hash or the receiver's check fails, and
     nothing is published. The cost is reading the source twice; for 692 MB on
     the host that is seconds.
   - **Alternative:** move `sha256` to the final frame, so the sender reads
     once. That changes the Pyxis-only OSC 5113 extension on both sides, and a
     mismatched peer then fails negotiation.
3. **Scope.**
   - **Default:** this work only removes the size limit for `xfer`, in both
     directions. It keeps the current framing, accepting about 25 minutes in
     QEMU (about 4½ natively) for the owner's data. Throughput is a separate
     follow-up after profiling the per-chunk cost; for uploads it would also
     involve the 4 KiB typeahead limit.
   - **HTTP(S) fetch bodies stay out.** Provider FILE snapshots are in memory by
     their own contract and keep their 16 MiB limit.
   - **Alternative:** include a window of several chunks in flight now. That
     changes the framing and needs a larger guest input allowance for uploads.

### Validation plan

- in QEMU, transfer a file larger than 1 GiB both ways and confirm the
  SHA-256;
- confirm that Pyxis and host memory stay flat during the transfer;
- show that cancellation and a forced hash mismatch leave no destination and
  no staging file, and that a full destination fails cleanly;
- repeat the baseline to show the rate is unchanged;
- the owner then sends the Diablo data to the stick.

### Streaming validation (2026-10-08)

Interactive QEMU 10.2.2 with nested KVM, 4 CPUs, 8 GiB, `VIRTIO_NET=1` and a
forwarded TCP port, as for the baseline. The guest ran userland `53fca52`,
since rebased onto userland main without changes as `2ade239`; the host
`pyxis-remote` was built from the same branch. Fixtures were random data.
Host memory is the RSS of `pyxis-remote`, sampled every minute; guest memory
is fastfetch's allocator figure.

- **1.1 GiB upload.** 1,153,433,723 bytes into `tmp://` were verified and
  published in 2492 s, about 0.44 MiB/s. Guest `sha256sum` matched the host.
  Host RSS stayed at 2088 KiB throughout. The guest allocator rose from
  112 MiB to 4.32 GiB, because the RAM file's growing heap buffer kept its
  pools mapped. Page-backed RAM files (2026-10-09) hold only the file's size.
  `xfer` itself holds one 64 KiB block.
- **1.1 GiB download, stopped deliberately.** The same file was sent from
  `host://` into a disk-backed host directory. After 51 minutes, at 480,811,008
  bytes (about 459 MiB), it was cancelled with Ctrl+C on the host. That run
  already showed flat memory: the guest allocator read 42.25 MiB before,
  43.80 MiB at 4 and 24 minutes, and host RSS stayed at 2108 KiB. Finishing
  would have taken about 80 more minutes and added only the final checksum,
  which the 100 MiB download below covers. The run doubled as the
  cancellation check: the host removed its 459 MiB staging file, no
  destination appeared, and the shell went on working.
- **100 MiB download.** 104,857,607 bytes from `host://`, past the old 16 MiB
  limit, were published on the host in 646 s, about 0.155 MiB/s, and its
  SHA-256 matched the source.
- **Source changed during a transfer.** Overwriting 16 bytes near the end of a
  15 MiB source after the first pass failed both directions before
  publication. `xfer send` reported "Source changed while it was being sent";
  `pyxis-remote` reported "Host file changed during upload", which the guest
  now prints as the peer's status. Neither side kept a destination or a
  staging file.
- **Full destination.** An 8 MiB tmpfs as the guest's `host://small` failed a
  15 MiB upload with `ENOSPC` (native status 24). The same size as the host's
  download directory failed with `ENOSPC: No space left on device`. Both
  directories were left empty.
- **Rate.** The 15 MiB baseline repeated with uploads of 32.8, 32.5 and 34.1 s,
  against 33.3–34.1 s. Downloads into a tmpfs host directory took 80.4, 80.9
  and 87.5 s, against 80.6–81.2 s; into the disk-backed directory, 85–97 s.
  Other agents shared this VM, so the spread is noise rather than a change.
  The 1 MiB round trip was also checked on the final build.
- **Not checked.** The receiver's own SHA-256 mismatch path was reviewed in
  code; producing it needs a sender that lies about its data. Writing to an
  npfs pool was not exercised in QEMU; the owner's transfer to the stick is
  that check.

## Transfer throughput

Assigned 2026-10-08, downloads first. Natively a 15 MiB `xfer send` took about
45 s, about 5.9 ms per 2 KiB chunk, while `xfer receive` took about 6 s, about
0.8 ms per chunk.

### Throughput decisions

Accepted by the owner 2026-10-08:

1. Fix the reply reads below, with no framing change.
2. Leave the TCP path alone until the owner re-times 15 MiB natively with that
   fix.
3. Keep one chunk in flight for now.

### Measurement (2026-10-08)

QEMU 10.2.2, nested KVM, 4 CPUs, `VIRTIO_NET=1`, forwarded port, sources in
`tmp://` unless stated. Packets were captured at the guest NIC with QEMU's
`filter-dump` and on the host loopback with `tcpdump`. Local `xfer` and server
builds carried temporary timing probes, since removed. Medians per chunk:

| Stage | Time |
| --- | --- |
| Host turnaround, guest data out → PROGRESS in, at the guest NIC | 0.39–0.44 ms |
| `xfer` source read, hash, encode and frame write | 0.05–0.07 ms |
| `xfer` reading one PROGRESS reply, 65–66 one-byte console reads | 5.1 ms |
| Guest network path: server TCP calls, segments and wake-ups | about 6.5–7 ms |

The hypotheses came out as follows:

- **A round trip per chunk.** Present in both directions by design; the host's
  share is under half a millisecond.
- **Nagle and delayed ACKs.** Not seen: no 40 ms or longer gaps, and each
  chunk's six segments leave back to back, about 0.22 ms apart.
- **Console output flushing or yielding per write.** Not significant: writing
  a 2.8 KB frame takes about 50 µs.
- **Host work per output chunk.** Not significant: about 0.4 ms including
  `pyxis-remote`'s per-byte parsing.
- **Found instead:** `xfer send` read each reply one byte at a time. Receiving
  already read in blocks. Each console read costs about 77 µs even with the
  bytes queued; why was not traced. The owner's native gap,
  (5.86 − 0.78 ms) / 66, works out to the same 77 µs, which suggests this is
  nearly all of the native stall. That is an inference until re-timed.

### Reply-read fix

`xfer send` now reads PROGRESS replies in blocks during the data phase and
returns to exact reads before `finish`, as receiving does (userland `dc0533d`).
Matched 15 MiB downloads in QEMU on the same day, with other agents loading the
host (load average about 1.7):

| Build | Times |
| --- | --- |
| Before, userland `a5a48b4` | 90.6, 97.8, 91.7 s |
| After, userland `dc0533d` | 52.6, 52.2, 56.9 s |

All downloads matched the source's SHA-256. A cancelled download left no
staging file, a command typed after a download ran normally, and a 1 MiB
upload still matched.

### Native re-timing (2026-10-09)

The owner timed 15 MiB transfers between the ThinkPad, wired and on AC, and
the desktop's `pyxis-remote`, built from the same revision as the PXE image:

| Main | To Pyxis | To the desktop |
| --- | --- | --- |
| Before the reply-read fix (2026-10-08) | about 6 s | about 45 s |
| `114f2ac`, with the fix | about 6 s | about 32 s |
| `183f793`, with [network throughput](../development/network-throughput.md) | 6.5 s | 7.5 s |

The reply-read fix took about 30% off downloads. The network throughput work
took the rest down to 7.5 s; in QEMU the remaining stall was Nagle holding each
frame's tail until the peer's delayed ACK, but natively its three changes were
not timed separately. Both directions now run at about 2–2.3 MiB/s with one
2 KiB chunk in flight; several chunks in flight remain an owner decision,
since it changes the framing and the guest's 4 KiB typeahead allowance.

## Out of scope

Directories, archives, resume of partial transfers, compression, transfers
through the local framebuffer console, and changes to the remote terminal wire
protocol.
