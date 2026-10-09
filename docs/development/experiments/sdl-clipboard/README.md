# SDL2 clipboard qualification

2026-10-09, nested QEMU, not native ThinkPad evidence. The
[interface](../../../interfaces/clipboard.md#graphics-and-sdl2) defines the contract;
[manual tool](../../../../ports/sdl2/manual/README.md) describes operator commands.
No automatic test, new CI or boot self-test was added.

## Sources and configuration

Baseline: Pyxis `89cc9f23` plus documentation-only merge `e9071a9d`,
userland `451c269a`, ports `8bff5dcd`. Matched timing candidate: kernel
`1c2707eb`, userland `4e6d8508`, SDL changes through ports `2ee4319`.
Final functional run: kernel `d4e772d8` (main `9fa80a16` merged), userland
`48ba9bad`, ports `7dbe43c5`. Final source adds readless keyboard-owner refusal,
rescue grant delegation and consumption of rejected shared commands after reset.

Builder: `pyxis-llvm23.1.3-49e2c1a`, ordinary `make -j16 image`; no compiler
rebuild. Optional DevilutionX image: `make -j16 image DIABLO_DATA=/shared/diablo-shareware`.
Only owner-mirror sources were fetched. The manual program is built separately:

```sh
make -f ports/sdl2/manual/Makefile SDK="$PWD/build/sdk" \
  SDL2_STAGE="$PWD/build/ports/sdl2/stage" BUILD="$PWD/build/manual-sdl-clipboard"
```

QEMU 10.2.2: Q35, nested KVM, `-cpu host`, four CPUs (one socket/four cores),
8 GiB, raw OVMF, standard VGA at 1280×800, UTC RTC, VirtIO RNG/NIC, no HDA.
Stock live configuration supplies both grants to Development and Read-only;
Remote has neither. Operator HMP key presses and explicit QMP key down/up events represent physical
PS/2 input.
The manual executable was uploaded through `xfer receive`, then, for final runs,
explicitly added to a local opt-in archive. It is absent from ordinary recipe
staging and the ordinary image. A second opt-in archive changes only Development to `multiplexer = true`,
`clipboard_local = true`, `clipboard_shared = false` for the refusal/forwarding
checks. Ordinary configuration is restored afterward. Game data and raw
logs/captures remain local.

## Matched idle cost

Three ten-second samples per state, from Linux `/proc/<qemu-pid>/task/*/stat`,
100 clock ticks/s. Percentages are of one host CPU: `utime+stime` for total,
`guest_time` for guest execution on QEMU's CPU 0/BSP thread. Whole-QEMU total
includes its other threads. This is unprofiled shared-host evidence.

| State | Baseline BSP guest / total | Candidate BSP guest / total | Whole QEMU baseline / candidate |
| --- | --- | --- | --- |
| Caelum selected, shells idle | 5.9–6.8% / 9.0–10.0% | 4.1–4.6% / 6.8–7.2% | 15.2–16.4% / 11.2–12.0% |
| Manual SDL program, waiting for input | 5.4–6.2% / 7.6–8.4% | 5.1–6.0% / 7.7–8.9% | 12.3–13.6% / 12.8–15.0% |

Both timing runs use identical manual source SHA-256
`b36152a60c5d4083c2559101208b7d2dea220f851cbf3c610c517e9914314d0f`,
linked against each revision's SDK/SDL. Later manual-only changes flush result
output and separate two-second focus checks from six-second expiry checks;
the idle event-wait path is unchanged. Ranges overlap for the SDL workload;
these short windows establish neither a speedup nor a consistent regression.
The comparison predates the main refresh and final refusal fixes; it is not an
isolated measurement of the final merged kernel. Source review finds no new
periodic clipboard polling: the worker parks until notification or action expiry.

## Manual observations

Final real SDL API calls, with stdout byte counts/hashes and visible results:

| Check | Observation |
| --- | --- |
| Local and shared text | ASCII LF/Tab (34 bytes) and multibyte UTF-8/CRLF (25 bytes) round-trip exactly; Has then Get works. Each layer retains its own item. |
| Empty and bounds | Empty publication succeeds, Has is false and Get is empty without a new error. Exactly 65,536 bytes succeeds; surrogate UTF-8 and 65,537 bytes refuse, preserving the prior item. |
| No authority | Unarmed and synthetic SDL commands return failed Set, false Has and empty/error Get; no cached success. A second immediate matched Get/Set refuses. Wrong-operation calls refuse. |
| Delivery identity | Overlapping local/shared commands cancel; a fresh Paste retrieves the old item. Batch delivery and explicit key-queue flush refuse rather than borrowing activation. |
| Lifetime | Source exit/new acquisition preserves published items. Read-only's local store starts empty while its shared Paste retrieves Development's UTF-8 item. |
| Focus and expiry | Two-second delayed Paste refuses after switching spaces, before expiry. Six-second delayed calls refuse after expiry. |
| Terminal boundary | Shared Unicode graphics text inserts nothing into the stock shell; no partial prefix or command execution. |
| Withheld shared grant | Local-only Development configured with `multiplexer = true`; foreground graphics launched through its pane shell. Shared commands neither publish nor insert, preserve Read-only's shared UTF-8 item, and do not block the next fresh local command. Unarmed APIs still refuse. |
| Held modifier after focus reset | Holding Super across a verified space switch and return, then pressing Shift+C/V, inserts no text into DevilutionX and creates no activation. After release, fresh local Copy/Paste works. |
| DevilutionX | New-hero name editor copies `PyxisLocal` locally and `PyxisShared` through Super+Shift; clearing then Paste restores each. Shared transfer leaves local text intact. Ctrl+X refuses publication and retains the selection. |

ASCII and UTF-8 round-trip FNV-1a hashes: `86D5E5054EEB0C1B` and
`5EB64298B4634C98`; the 64 KiB item is `12A4C7311A572325`.

Source-reviewed, not forced runtime cases: overlay and resize/remap revocation,
caller/owner mismatch, allocator/global-storage exhaustion, native queue loss,
callback/filter rejection and physical-input boundary races. All Has refusal
paths remain non-consuming. No native result or host clipboard bridge is claimed.
