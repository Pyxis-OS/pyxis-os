# Installer task 5 baseline — 2026-10-13

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
