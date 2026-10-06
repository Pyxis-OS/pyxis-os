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

**Until then:** a host file server such as dufs or `python3 -m http.server`
already serves uploads: `cat http://HOST:PORT/Foo.class > Foo.class` works today.

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
   should then also work directly inside kitty, without `pyxis-remote`.
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

## Tasks

- [ ] **1. Transfer by explicit command.**
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

- [ ] **2. Drag and drop.**
  - **Detection:** `tools/remote` enables bracketed paste on the host terminal.
    When one paste is exactly the path of one existing regular file, with the
    host terminal's quoting removed, the client offers: "Upload `Foo.class` to
    the current directory? [y/N]".
  - **Upload:** on yes, the client enters the task-1 receive command for that
    path at the shell prompt, and the transfer runs as in task 1.
  - **Fallback:** if the client cannot tell that the shell is idle at a prompt,
    the paste goes through as ordinary text. A dropped path must never be typed
    into a running program such as vi.
  - **Finish when:** dropping a file from a macOS and a Linux host terminal into
    an idle remote shell uploads it after confirmation. Dropping one into a
    running program, or pasting ordinary text, behaves as a normal paste.

## Out of scope

Directories, archives, resume of partial transfers, compression, transfers
through the local framebuffer console, and changes to the remote terminal wire
protocol.
