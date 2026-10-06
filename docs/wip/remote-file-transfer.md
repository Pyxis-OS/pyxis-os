# File transfer through the remote terminal

Status: **accepted, 2026-10-04.** The owner chose the frame format, the
confirmation policy and the scope [below](#owner-decisions). Any decision can be
revised later by the owner. Each task starts only when the owner says so.

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

The OSC 5113 extension is `px_sha256=1`, echoed in the initial `status=OK`.
Both sides require that echo before data. The sender includes `sha256=HEX` in
file metadata, and receivers verify that 64-digit digest and the declared size
before publication. Serialized kitty keys (`ac`, `fid`, `n`, `st`, `sz`, `d`)
retain their standard encodings. Uncompressed data chunks are 2048 bytes before
base64 encoding. Each implementation initially bounds buffered files at 16 MiB;
this is an implementation memory limit, not a protocol version.

For downloads, `send` also carries name, size and hash for the host's confirmation
before its initial permission reply. Then `file`, per-chunk `data`/`PROGRESS`,
`end_data`/file `OK`, and `finish`/session `OK` complete the transfer.
For uploads, `receive` plus one file query precede confirmation; the host replies
with session `OK`, one regular-file metadata frame (`st` is its actual file ID),
and catalog `OK`. The Pyxis program then requests that file and the host supplies
`data`/`end_data`. Per-chunk `PROGRESS` responses pace uploads too. After verified
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

## Out of scope

Directories, archives, resume of partial transfers, compression, transfers
through the local framebuffer console, and changes to the remote terminal wire
protocol.
