# Installer task 5 scratch qualification — 2026-10-13

Captured before task-5 code. Source is current main `c7a914e7`; its tracked tree
matches CI head `e3d3d696` exactly. Used that successful CI run [#2195](https://git.internal/PyxisOS/pyxis-os/actions/runs/2195)
image, with userland `fcb0c0e`, ports `4ed452df`, fs `414be0ac`. No kernel,
application, ABI or payload modification. Only the disposable live-medium menu
selects the existing `init=boot://init-install.pxe boot.install=1` entry directly.

Kernel SHA-256:
`9bc85d67409d6b6aeace60f8ee843e2b5b3f9276ff46b94dd62486a650b44c2b`.
Archive SHA-256:
`5a78d918395451b4a1998b1118357a5d47a96a850b5b75d20615e6fb9db39cac`.

QEMU 10.2.2, Q35/nested KVM, `-cpu max`, four vCPUs, 2 GiB RAM, UTC RTC,
fresh OVMF variables, modern VirtIO block, 512-byte sectors, writeback target,
VirtIO entropy, no NIC/USB/HDA. Booted from a disposable FAT live-medium image
containing the exact CI kernel/archive, avoiding the host's known AHCI CD-ROM
path. The target is a fresh 2 GiB raw file; Install chooses Read the room and the
12 MiB default journal. No native drive is attached or staged. No owned build
runs during measurements; shared-host load is uncontrolled.

## Results and timing boundaries

| Flow | Observed apply wall time | Whole-QEMU CPU | Result |
| --- | --- | --- | --- |
| Install, uninstrumented repeat | Between 122.6 and 174.3 s | Between 94.1 and 130.0 s through those observations | Success; complete copy/verification and EFI 5/5. |
| Update on the installed same-revision fixture | 270.588 s | 213.46 s | Success; full program stage and whole-ESP replacement. |
| Update before ESP writer entry | 242.336 s | 179.33 s | Includes initial revalidation and clearing the existing revision directory. |
| Update ESP writer entry to restart offer | 28.252 s | 34.13 s | Replacement, flush/release, raw EFI verification and final cleanup checks. |

Install is a manual observation bracket: at 122.6 s verification was still
running; success was visible at 174.3 s. The CPU bracket also includes any idle
time before the success observation, so it is not an exact apply CPU value.
Update uses observational hardware breakpoints at confirmed executor entry,
ESP writer entry and restart offer, filtered to the installer's address space.
Target function calls and memory writes are disabled. An ordered ELF relink's
LOAD bytes match the actual CI P1F byte-for-byte; no relinked executable is run.
`/proc` counters include all QEMU vCPU/device-emulation work, not just guest BSP.
These are single samples, not a performance comparison or native-media estimate.

An initial debugger-attached Install failed after 1,243 of 2,229 files with a
VirtIO request timeout and an incomplete target. Its timing is excluded; cause
is not established. The uninstrumented fresh-target repeat succeeds. No driver
fix or behaviour change belongs to this proposal baseline.

Both successful paths copy/verify 2,229 regular files / 46,730,754 payload bytes,
publish a 393,860-byte program manifest, then pass EFI 5/5 raw path/payload
checks (43,909,120 bytes). Programs are verified through the filesystem view and
may be cached; EFI reads follow raw flush/release. This is no power-loss proof.

## Behaviour before preservation

After successful Install and native clean shutdown in QEMU, a host preparation
adds a foreign `EFI/fedora/foreign.txt` sentinel and an owner comment to the
installed Limine file. Update offers the target, reports unknown/rebuildable
prior history, clears its existing 2,353 program entries and rewrites the ESP.
After success and clean shutdown, host inspection confirms **both the foreign
file/directory and the comment are gone**. This demonstrates today's destructive
behaviour only, not a real Fedora boot or coexistence qualification. The original
pool/kernel revision is known from the fixture; unknown history reported by the
current inspector remains unknown rather than being relabelled as a manifest-
backed Update measurement. Pool preservation and the still-active pool boot
configuration direction are separately established by current source inspection.

Native ThinkPad timing is **pending**, requiring an owner-present, explicitly
approved spare writable target. Today's destructive Update must never baseline
the internal Fedora ESP. The task-5 proposal records that native baseline and
later preservation/activation gates. All owned QEMU/GDB processes are stopped.
Raw images, screenshots, debugger scripts and logs remain local build artifacts.


## Buildable-scope checkpoint

[Userland #253](https://git.internal/PyxisOS/pyxis-userland/pulls/253) and
[parent #797](https://git.internal/PyxisOS/pyxis-os/pulls/797) implement frozen
reconciliation, private unlinked READ-only RAM FILE handoff, boot-bound complete
records and incremental owned-file publication behind
`INSTALLER_PUBLICATION_SCRATCH=1`. Default installed startup and Install/Update
remain unchanged. No native media was attached or written. Both PRs remain
draft; task 5 remains unchecked.

The current source consumes shared stager/boot-entry helpers from
[userland #272](https://git.internal/PyxisOS/pyxis-userland/pulls/272) at `1c8d49b6`
([Pyxis #880](https://git.internal/PyxisOS/pyxis-os/pulls/880)), on userland main
`a6040708`. Task-5 userland is `d5710c74`; fs is `22eaf447`, with the
[read-cost fix](../esp-verification-cost/README.md). The caller retains staged
objects through publication and closes them before mount retirement. Stage
already synchronizes/reopens/verifies; both final interval checks remain.
CHECK runs once, immediately after BEGIN. No lease extension or omitted hash.
The cleanup-only follow-up propagates an absent/stale CLOSE bookkeeping error
without replacing an earlier error; all owned closes are attempted and the result
is consumed. Focused default/scratch builds and source review pass. Runtime
evidence below retains its exact original revisions.
Canonical parent verification now requires ENUMERATE on the scratch caller's
root and traversal handles; its config executable links the shared child-open
helper. The focused current caller stages through ordinary canonical parents;
orderly interruption leaves only the unreferenced attempt, unchanged foreign
EFI/menu bytes and a clean FAT/GPT check. It does not qualify final activation.

Earlier smaller-image checks, at their original revisions, established:

| Check | Evidence |
| --- | --- |
| No history/catalog/record | Review offers trusted defaults without prior `config` use; Cancel leaves the whole image unchanged. |
| Unsupported handwritten Lua | Whole-archive selection and Cancel preserve exact source bytes; literal admission never executes it. |
| Stale Review inputs | Host-changed disposable input refuses before staging; no additional image changes. |
| Private FILE handoff | At `d1eb451`, all three unlinked exact-READ native FILEs were independently reread and compared. |
| Readiness selection | Prepared/mismatched-boot logical fixtures select archive and refuse apply; a complete matching committed fixture selects ESP. These are host-built logical fixtures, not completed publication. |
| Earlier small publication | At `b3a18fc`, real 1,510,032-byte kernel and 776,828-byte rescue archive stage and verify, but the old FAT32 implementation times out during interval revalidation. Foreign bytes/menu remain unchanged. |

Raw images/captures/debugger logs stay outside the repository. Earlier logical
fixtures and interrupted staging do not qualify durability or activation.

## Current functional refresh — 2026-10-15

Parent image `09dec965`, userland `0b2606d5`, fs `22eaf447`; LLVM builder
`df87791`, matching SDK/ports exports from the read-cost CI source. Full scratch
image and focused installer/config builds pass. Changed installer/config code
has no warnings; the full build retains existing httpfs enum-comparison and
fat32-inspect switch warnings. Default and scratch focused builds also pass.

Nested QEMU 10.2.2, Q35/KVM, host CPU, four vCPUs, 2 GiB, OVMF, standard VGA,
modern VirtIO block, 512-byte sectors. A disposable 768 MiB GPT disk has a
512 MiB mirrored FAT32 ESP / 4 KiB clusters. Its exact legacy normal/rescue menu
is disk-GUID-bound to a real old kernel/rescue pair; a foreign EFI sentinel and
Fedora menu block remain outside Pyxis ownership. This is a synthetic layout,
not Fedora boot qualification.

The extracted read-only certifier admits the legacy pair; Review pre-selects
legacy adoption, prints "owned switch last", and offers revision defaults
without prior configuration. Cancel exits 0 and the complete disk hash remains
identical. Manual ESP-GUID rendering is source-reviewed; coexistence integration
owns its runtime qualification. The pure planner adds no activation authority.

Kernel SHA-256:
`625bcfe08af3c464fb6f3e9a2614cd5f34054c9b017ebcd818153e9243cf794a`;
archive:
`327899b64822a13a0ecff29e3e4bc4cdb50753f5b6ed300aac96e3f89699260f`.

## Full-payload verification cost gate — 2026-10-15

The [matched read-cost record](../esp-verification-cost/README.md) shows
40,375,612-byte verification improving from 410.058 seconds to 12.935–13.207
seconds, all hashes matching. Reporting never paced the work. However, the
actual two-verification publication interval still lacks nested headroom.

A boundary-only, never-merged clock probe on parent `36d3f5fa`, userland
`96d69335` and fs `22eaf447` stages/syncs/verifies the full 5,930,128-byte kernel,
40,492,204-byte rescue archive and revision, then runs both final checks. CHECK
finishes at 0.175 s from before BEGIN; checks take 14.470 and 14.510 s. END is
at **31.839 s**. The post-verification guard returns TIMED_OUT before the Limine
switch: readiness is committed, boot activation pending, staged/record evidence
retained. There is no rollback/retry/reboot offer. Limine and foreign EFI bytes
compare unchanged. After normal guest power-off, all payload hashes match;
937 regular rescue files match source bytes; GPT and read-only `fsck.fat -n`
pass with a clean dirty mark. This is staging plus bounded refusal, not a
successful complete publication/activation result.

A separate earlier attempt hit a VirtIO 5-second FLUSH timeout after 146,880
kernel bytes, before BEGIN. Read-only GDB inspection found one uncompleted
FLUSH; no interval cost is claimed for it. The timing above uses a fresh fixture,
not automatic replay of the failed attempt. Staging remains slow before the
interval; the read fix claims no write or full-Update speedup. Native timing
requires an owner-approved scratch target and writer admission, even though
the read/hash probe itself makes no mutation. Neither internal NVMe nor a real
coexistence ESP is authorized.

## Remaining gates

Successful owned-entry activation, previous-pair cleanup, actual startup from a
committed boot-bound set, representative occupied/larger ESP lease headroom and
Daybreak's adversarial pass remain before release. The 30-second limit and all
checks remain unchanged. Native/installed activation still needs the owner's go;
internal coexistence additionally needs its accepted authority/qualification.

Classic Install/Update retain their fixed boot paths until task 5's committed
binding is delivered (owner decision relayed by epsilon, 2026-10-15). Coexistence
uses immutable attempts and manual entry text. Shared stager ownership evidence
is not a second active-binding record or automatic menu ownership.

Merge shared userland #272 then Pyxis #880, and fs #39 then Pyxis #882; task-5
userland #253 then Pyxis #797 follow only after their remaining gates. All owned
QEMU/debugger jobs are stopped at this checkpoint.
