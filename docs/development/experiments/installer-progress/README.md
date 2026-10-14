# Installer progress coverage — QEMU, 2026-10-11

Baseline parent `94d0320d`, userland `dca986b`; measured reporting code `d1d362a`.
Final userland adds only README corrections and the label “existing revision”
instead of assuming interruption.
Filesystem `b427df29`, ports `df3210b0`, lwIP `a1aadb91` and the kernel
implementation are unchanged. Both images copy 46 roots, 2,211 regular files
and 42,046,106 payload bytes. Installer grew 172,702 → 177,614 bytes; the live
archive grew 78,226,432 → 78,231,552 bytes. Rebuilt ports/SDK also regenerated
build-time/provenance strings; selected file sizes and the copied workload match.

Both full SDK/image/fs-tools builds passed using
`git.internal/pyxisos/pyxis-builder:pyxis-llvm23.1.3-49e2c1a` and
`make -j16 image fs-tools BOOT_MENU_TIMEOUT=60`; no new installer warnings.
Host Linux `6.19.10-300.fc44.x86_64`, QEMU 10.2.2, q35 nested KVM,
CPU `max`, four vCPUs, 2 GiB, HPET, UTC RTC, fresh raw OVMF variables,
standard VGA, read-only live ISO, one 2 GiB raw VirtIO target (512-byte sectors,
writeback cache honoring flush), VirtIO RNG, no NIC/HDA/xHCI device. Stock
Kconfig uses a 30-second npfs flush interval. Other agents' VMs can run on the
shared host; these are virtual costs, not native stick measurements.

## Matched costs

Install starts with a blank sparse target. Each Update starts with a restored
copy of the same installed, clean-journal, current-revision fixture containing
`home://kept.txt`. That exercises clearing/rebuilding the existing revision.
Runs alternate baseline/reporting with no builds during timed intervals.
Inspection covers the first source-open through target selection; Apply covers
accepted typed consent through the final success message, including claims,
removal, copies, syncs, verification and cleanup, excluding owner input/restart.
GDB uses four temporary hardware breakpoints in the ordinary installer build;
monotonic host timestamps and `/proc/<qemu-pid>/stat` give wall and whole-QEMU
user+system CPU time (100 Hz). CPU includes device emulation and all vCPUs.
No functions are injected. Raw captures and helper files stay local under build/.

| Flow / image | Samples | Apply wall seconds, median [range] | QEMU CPU seconds, median [range] | Inspection wall seconds, median [range] |
| --- | --- | --- | --- | --- |
| Install / baseline | 2 | 141.65 [141.16–142.15] | 120.02 [119.83–120.20] | 0.544 [0.531–0.556] |
| Install / reporting | 2 | 140.59 [134.64–146.54] | 116.86 [112.75–120.97] | 0.547 [0.491–0.604] |
| Update / baseline | 1 | 242.59 | not recorded | 0.241 |
| Update / reporting | 1 | 243.78 | 186.14 | 0.312 |

Install sample order was baseline/reporting/baseline/reporting; the Update
baseline ran before edits, with the same restored fixture used afterwards.
The first 149.52-second Install ran during another build and is fixture
preparation only, excluded above. One pre-boot debugger-connection failure was
also excluded. These few shared-host samples show no clear Install cost change;
Update differs by 1.19 seconds (0.5%). They do not establish a native throughput
or CPU cost, and the unrecorded baseline Update CPU time is not inferred.

## Verification meaning and qualification

Code inspection: copied-program verification uses `directory_sync`, then
ordinary FILE reads and byte comparisons. `npfs_store_read` uses retained data
pages populated by copying; sync and closing file handles do not evict them.
Cache misses can read the device. This is a filesystem-view check, without a
fresh-device-read guarantee; the owner's instant native stick-verification
observation is consistent with that path, without measuring its actual hit rate.
ESP verification separately uses raw DISK_READ after flush/release, bypassing
npfs data/metadata caches; this is device-command read-back, not physical-media
or power-loss evidence. Reporting changes neither verification behavior.

All four timed Installs and the matched Update pair reported success. The
reporting image copied and compared 2,211 files / 42,046,106 bytes, and its raw
EFI path/file checks reached 5/5; both known storage-write totals completed.
The same-revision Update exposed 2,321 removed entries before copying.
After main advanced, final integration was rebuilt with kernel `a48ef0b3` and
userland `79b2a80` (based on `24f3140`). An ordinary older-to-newer Update passed:
2,211 files / 42,052,346 bytes copied and compared, raw EFI checks 5/5 and
`updated`. This is qualification outside the earlier matched cost table.
The matched updated target-only boot retained `home://kept.txt`. Typed
cancellation left the raw target SHA-256 unchanged and issued zero writes.
The label rebuild reused verified unchanged SDK/ports; the address-parking
rebase also passed a full SDK/image/fs-tools build.

The console exposes source/archive/target inspection counters, removed entries,
logical copied/compared files and bytes, successful raw bytes, and activity lines
before blocking calls. Progress END is an observation boundary, not success or
durability. The non-console sink and clock/output-failure paths are inspected,
not fault-injected; future curses consumption is not implemented by this task.

Reproduce the ordinary manual flow with matching firmware paths (GTK is for
interactive replay; measurements used the non-window backend with manual QMP
keyboard input and screenshots):

```sh
make -j16 image fs-tools BOOT_MENU_TIMEOUT=60
truncate -s 2G build/target.raw
make run CPUS=4 MEMORY=2G ACCEL=kvm QEMU_DISPLAY=gtk \
  OVMF_CODE=/usr/share/edk2/ovmf/OVMF_CODE.fd \
  OVMF_VARS=/usr/share/edk2/ovmf/OVMF_VARS.fd \
  VIRTIO_BLK_IMAGE=build/target.raw
```

Select Install Pyxis in Limine, focus Install with Super+Right, then Install /
Read the room, default journal and typed `wipe`. Boot the installed target and
sync/power off before saving an Update fixture; boot live again, choose Update
and type `update`. Restore the target fixture before each measured run.

## Quiet display revision — QEMU, 2026-10-14

The owner accepted quiet progress and WRITE-only Caelum-console delegation to
the trusted installer. This follow-up changes reporting/Review, not disk I/O,
consent, verification or installed configuration activation. The general
userspace log-writer alternative is [technical debt](../../../technical-debt.md#userspace-kernel-log-writer).

Functional baseline: parent `c71f62e8`, userland `e3ba6e3`, successful main CI
image (Actions API run 2525). The first Limine entry was changed to the existing
Install-mode command line; packaged payloads were unchanged. Install and Update
completed on a disposable 2 GiB target. Inspection/Review and dozens of phase
lines scrolled past the visible screen. No timings were taken.

Reporting image: parent `33add54c`, userland `cec84f4`, fs `d84f8084`, ports
`2296a06c`, lwIP `a1aadb91`. The full image build passed with pinned builder
`pyxis-llvm23.1.3-e0801c2`, reusing matching main SDK/ports bundles whose sources
are unchanged by this applications-only task. Host QEMU 10.2.2, Q35, nested KVM,
CPU `host`, four vCPUs, 2 GiB, standard VGA/PS2 at 1280×800, VirtIO RNG and a
VirtIO-SCSI read-only boot CD, no NIC/HDA/xHCI. The disposable 2 GiB VirtIO disk
uses 512-byte blocks and writeback cache honoring flush. VirtIO-SCSI avoids the
host QEMU AHCI CD-ROM crash. Manual QMP keyboard input/screenshots and read-only
GDB inspection were used; raw images/logs/captures stay under local
`build/qualification/{baseline,quiet}`.

The normal Install Review fits with its prompt on one screen, showing 56 new
roots, 2,231 selected files and 47,429,393 bytes. A single copying/verification
row redraws with a bar, files and a measured byte rate. Short checks stay quiet;
major phases leave final lines. Install reached `installed` after copying and
comparing all selected files and raw EFI verification at 5/5. Detailed
BEGIN/END and coalesced UPDATE records appear in the Caelum/serial log, separately
from the Install console. GDB at the installer's console syscall observed a
serial-mirrored console capability with rights=1 (WRITE), transport=0, distinct
from its own non-serial Install console capabilities.
