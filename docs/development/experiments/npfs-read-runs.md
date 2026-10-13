# npfs bounded read-run qualification

Implementation base: `9c44ac0d1197d9cfca92c0c34da99e4722dbe69c`
(merged USB proposal). Code commit: `02676229f09dcf1d5cb948a0b38f99e349fcad1b`.
Pins remain userland `39499b92`, ports `1b171610`, fs `414be0ac`, lwIP `a1aadb91`.
Ordinary builds used `pyxis-llvm23.1.3-4c23a20`, `make -j8 image LOG_LEVEL=info`.
No dependency, USB-driver or public ABI changes; no new diagnostics or probe code.

## Nested QEMU

QEMU 10.2.2, Q35/KVM, four host CPUs, 2 GiB RAM, modern VirtIO boot/network,
qemu-xhci and usb-storage. Disposable GPT/npfs fixtures were read-only except
for a separate writable twin. The baseline kernel was built from untouched main;
the qualification kernel had the code above and a pre-commit revision string.
Their SHA-256 values are respectively
`a429ed92159c5aa09a776f51458cc85344983dbbc220356111a3af4cd2cea766` and
`9b4686518c1aa090013cf0fa2696997d6cd0d80ad01c2acffb5876c2497dc80a`.
Fresh current-main runtime/archive inputs served both. Full-size Clang used the
same existing toolchain payload in both boots, without adopting its build pins.

Commands through existing `pyxis-remote --machine --no-shell-echo`:
`<16 MiB echo image URI> <marker>`; `iobench read <fixture URI> --buffer 4088
--rounds 3`; Clang `--version` and `--sysroot=sysroot:// -c <tmp source> -o
<tmp object>`. Exit status, output marker/hash and FINAL/drain were checked.

| Serial groups of three unprofiled host-wall samples, seconds | Before | After |
| --- | --- | --- |
| 16 MiB image launch | 4.087 / 3.251 / 3.016 | 0.481 / 0.750 / 0.321 |
| Small-read iobench session | 1.777 / 1.699 / 1.888 | 2.184 / 3.342 / 1.889 |
| Empty session, excluding initial network setup | 0.020 / 0.022 | 0.064 / 0.025 |

These are **not interleaved acceptance measurements**. Small-read variation is
wider/higher after; no absence-of-regression claim follows from the unchanged
single-page code path. Native interleaved controls remain required. The first
empty session in each boot included roughly six seconds of network setup and
is excluded. A baseline Clang launch reached its 30 s guest deadline; it is a
failure, not a successful timing. After-code version and hello compile each
passed three times (ranges 1.721–3.463 and 2.060–4.505 s); all three object hashes
matched. Nested timings do not establish native gain.

Separate GDB observations at read boundaries: a warm 16,903,814-byte capture
issued 3,103 successful BOT READs before and 195 after, requesting the same
12,709,888 storage bytes. Both full capture hashes matched the independent host
source. These counts include metadata and are not per-request timings.

## Bytes, errors and cleanup

Host-built P1F fixtures retained a real small program and added a large read-only
segment. Healthy pools and the writable result passed host `fsck.npfs`.
Read-boundary GDB hashes matched complete host sources for 16,903,814-byte and
125,955,718-byte images, plus sparse and physically discontinuous twins.
A one-byte invalid P1F header captured completely and was rejected without a
child. An out-of-range mapping at logical page 40 returned `CALL_IO` and exactly
163,840 confirmed bytes matching the host prefix; no child was published.

A writable copy had 25 dirty cache pages at capture entry. Its complete hash
matched, it launched, explicit `sync <directory URI>` succeeded, and the host
checker accepted the resulting pool. This exercises dirty-page precedence while
ordinary small writes and reads retain their previous path.

GDB checked temporary staging (16 backed pages) and capture ranges (4,127 or
30,751 pages) returning to FREE with zero backing after success and rejection.
A 128 MiB minimal-runtime boot with RAM-file pressure also rejected an
out-of-memory launch cleanly. It did not establish staging-allocation fallback:
that branch, cancellation and an actual failed USB block ticket are source-reviewed,
not runtime-qualified. No fault injection was added. Malformed mapping input is
not evidence of device-error recovery. GDB scripts used no inferior function calls.

## Native handoff

The [native baseline](usb-read-throughput.md) identifies the original bottleneck.
A separate matched ordinary before/after packet uses this exact implementation
base, one shared qualification archive and the same stick/port. First post-boot
Clang launches are separated from repetitions, with empty/small-read controls,
output hashes and shared-worker responsiveness. Native runs are owner-operated
and **pending**; no native speedup or merge recommendation is claimed yet.
Private logs, raw fixtures, debugger scripts and operator-local selection remain
outside the repository. See the [accepted scope](../../wip/usb-read-throughput.md).
