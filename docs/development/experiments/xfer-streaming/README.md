# xfer qualification and throughput

Records for the [explicit transfer and drop](../../../userland/remote-terminal.md#explicit-file-transfer)
work, the streaming change (Pyxis #551, userland #165) and the reply-read fix
(#554, userland #167). Windowing is in [xfer pipelining](../xfer-pipelining/README.md);
the network changes are in [network throughput](../../network-throughput.md).
Protocol notes: [`xfer` README](../../../../userspace/xfer/README.md).

## Explicit transfer (2026-10-06)

Nested-KVM QEMU, four CPUs, 512 MiB, virtio-net and virtio-rng, OVMF, forwarded
TCP `2423:2323`; Linux host; fixtures in `/dev/shm`.

- A valid 60-byte Java 8 `.class` and a 1 MiB file uploaded with matching
  SHA-256; the 1 MiB download landed only in the named download directory.
- Guest NO_REPLACE refused an existing name and kept its hash; `--overwrite`
  replaced it. Existing host names were refused with staging cleaned. Downloads
  without `--download-dir`, a host refusal and a read-only `boot://` target
  failed clearly.
- Host Ctrl+C cancelled uploads and downloads with no target or staging name
  left; guest Ctrl+C sent cancellation; an unacknowledged guest cancellation
  reported its bounded timeout.
- Ordinary output and a multiline paste produced the same 60,099 host
  presentation bytes as the baseline client.
- Not run: hash-mismatch and unsupported-negotiation paths (reviewed in code
  only), corruption injection, macOS publication, persistent-backend durability.

## Drop (2026-10-07)

Same setup; the Linux client ran in a 100x24 PTY and bracketed paste was supplied
by hand, so **no GUI file drop was performed**.

- Backslash-escaped and double-quoted basenames, and a quoted `~/` path, uploaded
  with one confirmation and matching digests. An end delimiter split across two
  writes worked.
- `n`, Escape, pasting `y`, empty Enter and Ctrl+C behaved as specified; Ctrl+C
  cancelled a dropped 16 MiB transfer with nothing left behind.
- In vi insert mode a dropped path was literal text with no upload offer; after
  vi exited, detection stayed off until reconnect.
- Machine mode kept OSC 133 bytes (default) or none (`--no-shell-echo`).
- `fj pr status` failed to parse Forgejo's empty status value, so no dependency
  CI pass was claimed. Not measured: Linux GUI drops, macOS drops and publication.

## Streaming (2026-10-08)

Nested-KVM QEMU 10.2.2, four CPUs, `VIRTIO_NET=1`, forwarded port.

Baseline, each file random data in `tmp://`, before streaming (16 MiB cap):

| Direction | Size | Times | Rate |
| --- | --- | --- | --- |
| Upload | 15 MiB | 33.3, 34.1, 33.5 s | about 0.46 MiB/s |
| Download | 15 MiB | 81.2, 80.6, 81.1 s | about 0.19 MiB/s |

Native (ThinkPad PXE, wired LAN, owner stopwatch): upload about 6 s, download
about 45 s for 15 MiB.

Streaming validation (8 GiB guest):

- 1.1 GiB upload verified in 2492 s (about 0.44 MiB/s); host RSS stayed at
  2088 KiB. The guest allocator rose to 4.32 GiB only because the RAM file's heap
  buffer kept its pools mapped; page-backed RAM files (2026-10-09) hold just the
  file size. `xfer` held one 64 KiB block.
- A 1.1 GiB download from `host://` was cancelled deliberately at about 459 MiB
  after 51 minutes. Memory stayed flat (guest 42.25 to 43.80 MiB, host RSS
  2108 KiB), the host removed its staging file, no destination appeared.
- 100 MiB download (past the old limit) published in 646 s with matching SHA-256.
- A source overwritten after the first pass failed both directions before
  publication, with no destination or staging file. A full 8 MiB destination
  failed a 15 MiB transfer with `ENOSPC` in both directions and left nothing.
- Rates matched the baseline within noise (other agents shared the VM).
- Not checked: the receiver's own SHA-256 mismatch path (needs a lying sender);
  writing to an npfs pool in QEMU.

## Reply-read fix (2026-10-08, 2026-10-09)

Per-chunk medians in QEMU: host turnaround 0.39-0.44 ms; `xfer` read, hash,
encode and write 0.05-0.07 ms; reading one PROGRESS reply, 65-66 one-byte
console reads, 5.1 ms; guest network path about 6.5-7 ms. Nagle, delayed ACKs,
console flushing and host parsing were not the cost then. `xfer send` read each
reply a byte at a time (about 77 us per console read, matching the native gap);
it now reads in blocks during the data phase (userland `dc0533d`).

| 15 MiB download in QEMU | Times |
| --- | --- |
| Before, userland `a5a48b4` | 90.6, 97.8, 91.7 s |
| After, userland `dc0533d` | 52.6, 52.2, 56.9 s |

Native, owner stopwatch, wired and on AC:

| Main | To Pyxis | To the desktop |
| --- | --- | --- |
| Before the fix | about 6 s | about 45 s |
| `114f2ac`, with the fix | about 6 s | about 32 s |
| `183f793`, with network throughput | 6.5 s | 7.5 s |
