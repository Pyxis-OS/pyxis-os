# Saved shell history

Measured on 2026-10-09 for the shell history debt paydown. The owner accepted
three decisions on 2026-10-09 (#606):
- one `home://.history` per home;
- merge and replace on every saved line, without a sync, because native
  volumes flush the moved file before a replacing rename;
- lines starting with a space kept in memory only.

The behaviour is in the [shell guide](../../../userland/shell.md#commands-and-quoting).

## Configuration

- **QEMU:** 10.2.2 with the local AHCI fix, nested KVM on the development VM,
  q35, 4 CPUs, 8 GiB, VirtIO net with a forwarded port.
- **Builds:**
  - **Before:** main `67ee3b2` with userland `9ea770b`.
  - **After:** this branch with userland `72f303d`, rebased on userland main
    `047859a`.
- **Installed image:** this branch installed by its own installer onto a
  sparse 2 GiB raw VirtIO disk in RAM, then booted from that disk alone.
  - **Override:** a pool override, `system://config/boot.lua`, added a
    `remote` space with `home://` read-write, so the installed system had a
    remote session for timing.
- **Workload:**
  - **Batch:** 200 builtin commands sent at once through one remote session,
    timed until a final `echo` marker printed.
  - **Saved lines:** `cd home://` and `cd tmp://` alternate, so each line is
    recorded.
  - **Unsaved lines:** the same lines with a leading space, which keeps them
    out of the file in the same build.
  - **Typing:** a leading space adds one echoed character per line.

## Results

| Home | Build | Saved lines, s per 200 | Space-prefixed lines, s per 200 |
| --- | --- | --- | --- |
| Live RAM | Main | 24.42, 24.48, 22.84 (no file) | 26.45, 25.16, 25.07 |
| Live RAM | This branch | 20.52, 20.55, 20.41 | 21.75, 21.95, 21.92 |
| Installed npfs | This branch | 28.86, 28.69, 28.83 | 22.67, 23.01, 21.04 |

The main row used identical repeated lines, which aren't recorded, so it is a
plain command-rate baseline rather than a saving comparison.
- **RAM home:** saving cost nothing measurable.
- **Installed npfs:** about 30 ms per saved command in nested QEMU, with a
  6.6 KB file. The cost is the rewrite and the replacing rename's flush and
  commit on a RAM-backed raw disk. Native cost is unmeasured.

**Checks,** all in QEMU on this branch:
- **Order:** two remote sessions saving alternately left their lines
  interleaved in order. A new session's Up recalled the newest saved line.
- **Worst-case contention:** two sessions each sent 101 lines at once, with
  no pause between saves. The file kept 196 of 202, which is the accepted
  "later rename wins" loss.
- **Killed session:** after `pyxis-remote` was closed, every line that session
  had submitted was saved, its unsubmitted line was not, and no `.history.HEX`
  file was left.
- **Damaged file:** a control byte, a 2000-byte line, a space-prefixed line and
  a final line without LF were all skipped on load. The next save rewrote the
  file with only the valid lines.
- **Bounds:** a 1500-line file was trimmed to its newest 1000 lines. A 100 KB
  file of 1 KB lines was trimmed to 64,751 bytes, at a line boundary.
- **Read-only space:** no message; Up recalled that shell's own lines.
- **Installed image:** lines saved before `reboot` were in `.history` after it,
  Up recalled them, and the space-prefixed line was absent.
- **Not exercised:**
  - a full volume, so the single failure message was checked in code only;
  - the rescue entry, which has no menu on an installed disk and still mounts
    the optional home when it can;
  - a space without `home://`, which the stock configurations don't have.
