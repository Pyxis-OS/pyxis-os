# File transfer through the remote terminal

Status: **implemented**, awaiting the owner's GUI-drop check. Explicit transfer
(`xfer send` / `xfer receive`) and drag-and-drop upload are specified in the
[remote terminal guide](../userland/remote-terminal.md#explicit-file-transfer)
and the [`xfer` notes](../../userspace/xfer/README.md). Pull requests: explicit
transfer #445, drag and drop #455 (with userland #137), streaming #551 (userland
#165), reply-read fix #554 (userland #167), windowed `px_xfer=2` #600. Measurements:
[xfer qualification](../development/experiments/xfer-streaming/README.md) and
[xfer pipelining](../development/experiments/xfer-pipelining/README.md).

## Owner decisions

Accepted 2026-10-04, revised 2026-10-06.

1. **Frame format:** kitty's [file transfer protocol](https://sw.kovidgoyal.net/kitty/file-transfer-protocol/)
   (OSC 5113), single regular files only, with a mandatory negotiated SHA-256
   extension (now `px_xfer=2`). Peers that do not advertise it are refused;
   stock-kitty interoperability is not a goal.
2. **Confirmation policy:** every upload and download is confirmed on the host;
   an existing Pyxis file is never overwritten without an explicit flag;
   downloads are saved only into a download directory named on the
   `pyxis-remote` command line (none, no downloads); names arriving from Pyxis
   are reduced to a plain file name.
3. **Scope:** single files. Directories and projects come from porting a small
   archive tool later, then transferring the archive.
4. **Drop (2026-10-07):** one absolute or `~/` path in a bracketed paste at a
   known empty prompt; the drop confirmation authorizes only the matching
   upload, with no `--overwrite`; elsewhere the paste is ordinary text. The
   prompt-tracking limit is in
   [technical debt](../technical-debt.md#remote-drop-prompt-tracking).
5. **Streaming (2026-10-08):** no fixed size limit; unverified bytes reach disk
   only under the private staging name; the sender hashes the source in a first
   pass and fails if it changes. HTTP(S) fetch bodies stay out, and provider
   FILE snapshots keep their in-memory 16 MiB limit.

## Open task

- [ ] **Owner GUI-drop validation.** Dropping a file from a macOS and a Linux
  host terminal into an idle remote shell uploads it after confirmation;
  dropping one into a running program, or pasting ordinary text, behaves as a
  normal paste. Only manual bracketed-paste input on Linux was tested, and macOS
  publication (`renameatx_np(RENAME_EXCL)`) is unmeasured. After this passes,
  delete this document, fold decisions 1-5 into the remote terminal guide and
  update the links to it.

## Out of scope

Directories, archives, resume of partial transfers, compression, transfers
through the local framebuffer console, a progress display, and changes to the
remote terminal wire protocol.
