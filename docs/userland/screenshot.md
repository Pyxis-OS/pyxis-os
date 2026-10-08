# Screenshot command

`screenshot PATH` saves the currently shown local screen as a non-interlaced
8-bit RGB PNG. Success is quiet. The image includes navigation, the selected
space's shown layer, clipping/background margins and the visible software
cursor. It uses the independent [CAPTURE resource](../interfaces/screen-capture.md),
which grants no additional path access. Missing CAPTURE fails before opening
the output path; DRAW alone is insufficient.

Live Development and Remote and installed `pyxis` receive CAPTURE under the
[`screenshot = true` boot policy](init.md#boot-configuration). Read-only and the
built-in rescue space do not. Any command holding that grant can observe the
whole local screen, independently of the shown space's authority. The
[LAN visibility debt](../technical-debt.md#kernel-log-retention-and-lan-visibility)
records the consequence for the unauthenticated Remote terminal.

## Guest output and host download

The path is required and follows the caller's existing roots and working
directory. Missing parents are not created. A directory, trailing slash or
final `.`/`..` is not an output filename. For a remote download:

```text
screenshot tmp://screen.png
xfer send tmp://screen.png
```

Connect the host client with its existing `--download-dir`:

```sh
build/tools/pyxis-remote --download-dir ./downloads 127.0.0.1 2323
```

The host keeps its confirmation and refusal to overwrite a host name. These
remain two explicit guest commands; there is no one-step host capture command.
File transfer is limited to 16 MiB per encoded PNG. A noisy 4K frame can exceed
that limit: it can still be saved locally, but download is refused.
`tmp://` is RAM-backed and disappears on reboot.

## Staged replacement and failure ownership

The destination parent needs LOOKUP, CREATE, WRITE_FILES and REMOVE. The command
holds that parent through reservation, publication and cleanup. Authority to
write an old file alone is insufficient; there is no direct-write fallback.

After capture succeeds, the command exclusively creates a sibling temporary
file, encodes into it, completes the PNG and closes the snapshot and output
FILEs. Only then does rename-with-replace publish the result. Failure before
publication preserves an existing destination. Success replaces its object;
older held handles continue to refer to the old object.

Names are `.screenshot-` followed by 16 hexadecimal digits, starting at zero.
At most 64 candidates are considered; only confirmed name collisions retry.
Candidates equal to the destination leaf are skipped. Colliding files are never
opened or truncated. As with [cp](cp.md#failures-and-limits), other writers must
leave the reserved temporary name/file alone.

Capture, allocation, native I/O, PNG and close failures return nonzero. Before
publication, the command closes its handles and attempts one removal of a
confirmed reservation; an uncertain WRITE changes no reservation ownership.
Unconfirmed creation reports the possible temporary name without deleting it.
After publication is attempted, a failed reply reports both names without
retrying or deleting uncertain state. Cleanup failures report possible
leftovers. Abrupt termination can leave a temporary file, with no automatic
stale-file removal. A parent-close failure after publication does not undo the
completed replacement.

Closing and renaming alone promise no crash durability. When persistence is
needed, synchronize both the file and its parent, for example:

```text
sync home://screen.png home://
```

## Encoding and limits

The command reads one immutable native 32-bit row at a time and converts the
reported RGB channel shifts into RGB8. It uses the snapshot pitch and extent,
conventional libpng error recovery and row APIs, compression level 3 and ordinary
libpng filtering. Userland holds one native row, one RGB row and library working
storage; it does not allocate another full image. Libpng and zlib remain
reusable [ports development exports](../development/build-bundles.md#local-reuse).

The raw snapshot costs `4 * width * height` bytes until closed. Its bytes freeze
one presenter composition and preserve existing single-buffer tearing; no
atomic application frame, vblank or exact scanout timing is promised. A panic or
stuck presenter/scheduler cannot complete capture. Capture requests do not
retry BUSY, allocation refusal or backend failure automatically.

## Qualification

On 2026-10-08, the ordinary image built with LLVM 23.1.3 / 49e2c1a booted
in q35 QEMU with standard VGA, a 1280x800 boot framebuffer, four CPUs, 2 GiB
and nested KVM. Remote commands produced host-decoded RGB PNGs of the Caelum
and Development TTY screens (64,208 and 18,563 bytes). The existing `xfer send`
confirmation, SHA-256 verification and host publication completed; a repeated
host filename was refused without changing its earlier hash.

A second capture replaced the guest destination. Read-only's missing-CAPTURE
refusal preserved an existing file, verified by its hash; a boot-archive output
failed parent authority before staging. Publishing over an existing directory
failed and reported both possible names, leaving the directory and temporary
intact. A later capture skipped that colliding temporary without truncating it.
Debugger inspection found no pending/active capture or private backing after
success. Allocation, partial-I/O and uncertain-write cleanup were source-reviewed,
without fault injection. Target PNG decoding, driver/layer/resize comparisons,
matched presenter/encoder cost and native ThinkPad capture remain milestone
qualification work.
