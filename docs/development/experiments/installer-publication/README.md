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
[parent #797](https://git.internal/PyxisOS/pyxis-os/pulls/797) implement shared
reconciliation, frozen snapshots, native RAM FILE handoff, boot-bound readiness
and incremental owned-file publication behind `INSTALLER_PUBLICATION_SCRATCH=1`.
Default installed startup and Install/Update remain unchanged. Both PRs remain
draft: successful publication and owned-entry activation are not qualified.
No native media was attached or written.

The functional runs use the same Q35/KVM, four-vCPU, 2 GiB, modern VirtIO block
profile above, but an 80 MiB disposable GPT disk with a 40 MiB FAT32 ESP and a
minimal live archive. These are not matched performance samples against the
full-profile baseline. The retained foreign fixture is an EFI file plus a
Limine comment/Fedora block; this is not a Fedora boot qualification.

| Check | Observed result |
| --- | --- |
| No history/catalog/record | Review offers trusted revision defaults, pre-selected Apply, without prior `config` use. |
| Cancel | Whole target image hash unchanged. |
| Unsupported handwritten ESP Lua | Explicit whole-archive selection and Cancel preserve the exact source bytes and whole image. Declarative non-execution is additionally source-inspected. |
| Host fixture changed after Review | Initial exclusive revalidation refuses before staging; whole image hash equals the post-fixture-change hash. |
| Private FILE handoff | At userland `d1eb451` / parent `1168b182`, three unlinked native exact-READ FILEs were created and independently reread/compared; subsequent stale refusal leaves the target unchanged. |
| Readiness selection | Read-only logical fixtures: prepared record selects archive and blocks apply; matching committed record plus exact triple/catalog selects ESP; mismatched boot identity selects archive. Cancel preserves each whole-image hash. |
| Small-volume publication | At userland `b3a18fc`, payload and configuration staging/verification complete, but interval revalidation returns `CALL_TIMED_OUT`, phase EXCLUSIVE, before canonical readiness publication. No successful-publication claim. |

The last run uses a debug-stripped real kernel whose LOAD segments match the
full kernel byte-for-byte (1,510,032 bytes), a real filtered rescue archive
(776,828 bytes), and a 13-byte revision file. Kernel/program identity is
`aad94221863a`; catalog identity is independently `b3a18fc5eb89`. Read-only GDB
inspection confirms the consumed ESP domain/partition and binding sizes;
target function calls and memory/register writes are disabled. The publisher
retains its attempt, ownership manifest and staged config/prepared/committed
artifacts. After native clean shutdown, host inspection confirms no canonical
`config.record` or selected Lua/catalog names, and byte-identical foreign EFI
content and Limine file. Artifacts are retained for manual disposition.

The earlier larger-payload attempt was cleanly powered off while staging its
fresh kernel file, before readiness or menu changes. It is interruption-state
evidence, not simulated power loss or completed publication.

A progress-label defect observed in the small run mixed verification bytes
with the preceding revision-file copy total. Userland `b028cba` separates each
copy/verification extent; it changes reporting only. Default and scratch image
builds pass with the existing `4c23a20` compiler container. Full native/installed
activation, successful menu switching, previous-pair cleanup and full-profile
lease headroom were unqualified at this checkpoint. The old FAT32 preflight cost
blocked this run; no lease extension or omitted revalidation was introduced.
The delivered core and current integration are recorded below.
The readiness fixtures were made on host-side disposable copies by placing the
native staged selected files and prepared/committed record at canonical names.
They qualify logical selection only; they do not certify that publication
completed or that the installed loader activated. A hardware breakpoint after
the chooser returns confirms the matching fixture's ESP selection without
calling target functions or changing target memory/registers.
All owned QEMU/debugger jobs are stopped at the checkpoint. Raw disks, captures
and debugger logs stay in local build artifacts.

## Shared-stager integration refresh, 2026-10-15

The parent rebases onto main `ffee27f4` and pins published userland `f29de7f3310b`.
That userland rebases the existing task-5 commits onto shared-stager `f14dc1325d35`
([userland #272](https://git.internal/PyxisOS/pyxis-userland/pulls/272)). The helper
remains unchanged. The caller stages kernel/archive/revision through it, retains
its evidence through configuration publication and closes it before mount
retirement. Its three-payload ownership record matches the existing cleanup
reconstruction. Stage already synchronizes/reopens/verifies, so the redundant
immediate verification pass is removed; both final interval-boundary checks remain.

The delivered FAT32 core and current publisher's single CHECK immediately after
BEGIN are included. The separate
[128 MiB editor qualification](../fat32-preflight-cost.md#actual-128-mib-editor-publication-2026-10-14)
completes under the unchanged 30-second interval. It is not full task-5 installer
publication evidence. Positive task-5 publication, owned-entry switching,
previous-pair cleanup and full-profile lease headroom remain unqualified.

Both full default and scratch userland builds pass with the existing `df87791`
compiler and the selected SDK/ports exports. Installer/config/session
builds have no warnings; the full build retains existing httpfs enum-comparison
and fat32-inspect switch warnings. This parent refresh has not been built or run.
The earlier QEMU results above retain their original revisions; this refresh adds
no new task-5 runtime result or native-media evidence.

The current main installed-menu allowlist and quiet progress/WRITE-only private
diagnostic delegation are retained. Fresh Install generates only normal/rescue
entries. Preservation Update changes only reviewed owned/adopted spans and
retains outside bytes; old unowned tool entries require separate reviewed removal.
No live-menu template is copied. Installed startup and shipped Install/Update
remain unchanged behind the default-off scratch gate.

Dependency order is shared stager [userland #272](https://git.internal/PyxisOS/pyxis-userland/pulls/272)
then [Pyxis #880](https://git.internal/PyxisOS/pyxis-os/pulls/880), followed by
[userland #253](https://git.internal/PyxisOS/pyxis-userland/pulls/253) then
[Pyxis #797](https://git.internal/PyxisOS/pyxis-os/pulls/797). The stager and task-5
PRs remain draft. Full installer-profile qualification and adversarial review
remain before release; native writes and installed activation remain unauthorized.
