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
single-page code path. The native interleaved controls below resolve this concern
for the measured workload. The first
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

## Native A/B, 2026-10-13

Owner/orchestrator hands-free qualification on the ThinkPad T14 Gen 1 AMD,
PXE and AC power, with the same SanDisk USB 3.2 Gen1 toolchain stick/port.
All four boots had 12 CPUs online. A1/B1/A2/B2 used ordinary `LOG_LEVEL=info`,
identical boot options and the common
qualification archive (SHA-256
`4a481e76b484eae008cbab2e943e18990314d060ddf014f0193e639344bb5c98`).
A was untouched `9c44ac0d`, kernel hash `a429ed92…` above; B was `2d8e3653`,
kernel SHA-256 `e9e8add54c27bc6d62c10f129e4dbfc0cb93a645f09760c8be57a3785d903ce7`.
There was no debugger/probe during these measurements. Source code is unchanged
by this subsequent documentation update.

The existing client used `--machine --no-shell-echo --listen <beacon name>
0.0.0.0 2323`. Each boot ran three empty/version/hello-compile/hash/small-read
rounds. The small-read command was `head -c 1048576 <Clang executable URI> |
sha256sum`; source/object fixtures were unique RAM-backed `tmp://` files.

| Boot | Version 1 / 2 / 3, seconds | Hello compile 1 / 2 / 3, seconds | 1 MiB small-read range, seconds |
| --- | --- | --- | --- |
| A1 | 4.550 / 4.283 / 4.288 | 4.297 / 4.296 / 4.300 | 0.0175–0.0186 |
| B1 | 1.193 / 1.113 / 1.115 | 1.124 / 1.126 / 1.125 | 0.0176–0.0184 |
| A2 | 4.564 / 4.296 / 4.296 | 4.311 / 4.308 / 4.311 | 0.0172–0.0205 |
| B2 | 1.191 / 1.114 / 1.113 | 1.127 / 1.123 / 1.127 | 0.0174–0.0212 |

Repeated versions improved from 4.283–4.296 to 1.113–1.115 s; hello compiles
from 4.296–4.311 to 1.123–1.127 s, about **3.8× faster**. Version 1 is the first
Clang launch after each boot and is kept separate, without claiming forced cold
media. Empty-session ranges were 5.0–7.1 ms before and 5.1–6.2 ms after;
small-read ranges overlap, with medians 18.19 and 18.24 ms. No material control
regression was observed in this campaign.

All required command results and FINAL/drain checks passed. Each boot's three
object hashes matched; unique source names make cross-boot object hashes differ.
Every small-read prefix matched independent host extraction. Optional inventory
failed on both kernels because utilities were absent; hardware provenance comes
from the owner. A Tailscale interruption and overlapping orphaned runner invalidated
an earlier A2/B2 attempt; those captures are excluded. Clean A2/B2 used one runner.

Native responsiveness is **not qualified**: the second beacon session on TCP
2324 never received READY, so the wrapper could not run its concurrent check.
The successful two-session QEMU observation is not a native substitute. Allocation
fallback, cancellation and real failed-ticket qualification retain the limits above.
The owner restored ordinary main `50e5411f` afterwards. Private clean and discarded
captures remain in the staging packet; no real GUID or host account path is in
repository records. See the [accepted scope](../../wip/usb-read-throughput.md).
