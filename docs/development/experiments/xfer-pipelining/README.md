# xfer pipelining

Measured on 2026-10-09 for the xfer debt paydown. The owner accepted three
decisions on 2026-10-09 (#592):
- a 64 KiB window of file data in 2 KiB chunks, with cumulative PROGRESS
  replies at least every 16 KiB;
- today's cancellation contract, discarding at most one window;
- `px_xfer=2` replacing `px_sha256=1`, with no fallback.

The protocol is described in the [`xfer` notes](../../../../userspace/xfer/README.md)
and the [remote terminal guide](../../../userland/remote-terminal.md#explicit-file-transfer).

## Terminal output admission

The window alone made QEMU downloads slower: 15 MiB took about 40 s instead of
25. A packet capture showed one OUTPUT frame about every 0.93 ms, most with a
6–58-byte payload. The cause was in the kernel terminal:
- the window now filled the 64 KiB output queue between `xfer` and the remote
  server;
- the kernel then admitted a blocked write in pieces sized to the space each
  drain had just freed;
- the server forwards one record per round, so each round carried a fragment.

Before the window, `xfer` never had more than one frame queued.

Output writes are at most 4 KiB, so a write now waits until its whole record
fits ([terminal sessions](../../../userland/terminal-sessions.md#queues-and-operations)).
That stays within the documented contract: output was allowed, not required,
to accept a short prefix. An alternative is for the remote server to coalesce
data records into one frame, leaving the kernel as it was. It would also have
to hold back non-data records.

## QEMU results

**Configuration.**
- **QEMU:** 10.2.2 with nested KVM on the development VM, q35, 4 CPUs, 8 GiB,
  VirtIO net with a forwarded port.
- **Builds:**
  - **Before:** main `7910bee`, with its own `pyxis-remote`.
  - **After:** this branch, with its own `pyxis-remote`.
  - **Window only:** this branch before the terminal change, for reference.
- **Files:** random data uploaded into `tmp://`, then downloaded into a host
  directory.
- **Timing:** from pressing `y` at the confirmation until the prompt returned,
  by a local tmux script. Only that remote session was connected.

| Transfer | Main `7910bee` | Window only | Window and terminal change |
| --- | --- | --- | --- |
| 15 MiB up, s | 19.02, 19.02, 18.82, 18.44 | 11.32, 11.64, 13.32 | 13.21, 12.70, 12.80 |
| 15 MiB down, s | 26.23, 24.69, 26.07 | 42.30, 43.54, 39.61 | 11.67, 11.57, 10.63 |
| 64 MiB up, s | 81.02 | — | 53.97 |
| 64 MiB down, s | 108.24 | — | 48.88 |

Every transfer matched its SHA-256.

**Where time goes.** In nested QEMU both directions keep the BSP vCPU at about
100% of a host CPU, while the other vCPUs stay at 5–30%. Nested QEMU has no
usable TSC, so clock reads are HPET exits, and the network worker and remote
server rounds on the BSP set the rate. These figures show a regression check
and relative change. They do not predict native rates.

**Checks** (QEMU, this branch):
- **Host Ctrl+C mid-upload and mid-download:** each cancelled through the
  handshake, and left no staging file on either side.
- **Host file changed during an upload:** failed with "Host file changed during
  upload" and published nothing.
- **Mismatched builds:** an old `pyxis-remote` refused the new `xfer` both ways
  with its old message. The new `pyxis-remote` refused main's `xfer` with
  "Transfer protocol px_xfer=2 is required; build pyxis-remote from the same
  revision as the Pyxis system".
- **Shell input:** the shell ran normally after every check.

## Native steps for the owner

On the ThinkPad, wired, on AC, with PXE builds of main before this change and
of this change. Use `pyxis-remote` built from the same revision as each image.
- **Session:** connect only that one `pyxis-remote` session, with no other
  remote or monitoring session, and leave the local console idle.
- **Free memory:** check it with `fastfetch`; 700 MiB in `tmp://` needs that
  much free.

On the desktop, create the files once:

```sh
mkdir -p ~/xf
head -c 15728640 /dev/urandom > ~/xf/f15m.bin
head -c 734003200 /dev/urandom > ~/xf/f700m.bin
sha256sum ~/xf/f15m.bin ~/xf/f700m.bin
```

Connect with `pyxis-remote --download-dir DIR ...`, using a disk-backed `DIR`.
In the remote shell, `cd tmp://`. Time each transfer with a stopwatch from
pressing `y` to the prompt. Between downloads, remove `x.bin` from `DIR`.

1. **15 MiB,** three times each way on both builds:

   ```text
   rm x.bin
   xfer receive xf/f15m.bin x.bin
   xfer send x.bin
   ```

2. **700 MiB:** once each way on main (about 5 minutes each), and three times
   each way on this change. Use the same commands with `xf/f700m.bin`.
3. **Checks:** compare `sha256sum DIR/x.bin` with the source, and note
   `fastfetch` memory before and after.

After the change, 15 MiB may take under a second, close to stopwatch
resolution, so the 700 MiB runs carry the rate.

## Limits

- **Native rates are unmeasured.**
- **Next bound, natively:** unknown. Candidates are per-frame work (base64,
  SHA-256, OSC parsing and terminal copies), the remote server's rounds, and,
  for uploads, the 4 KiB guest input queue, which this work kept unchanged.
- **Cancellation:** it discards at most one window of data.
