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
userspace log-writer alternative is [technical debt](https://git.internal/PyxisOS/pyxis-os/issues/1487).

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

The baseline ESP template also contains FAT32 inspection boot entries. The
unchanged Update checker classifies that as rebuildable boot damage and reports
an unknown previous revision; its quiet Review preserved that distinction.
For the known-revision case a scratch copy of the baseline ESP was narrowed to
its existing normal/rescue entries using host `mcopy`, preserving disk identity
and npfs payloads. Review then showed one updated root (746,273 → 748,977 bytes)
and 55 unchanged roots (46,680,416 bytes), with no new/removed roots. Empty typed
consent refused and the full raw-disk SHA-256 remained identical to its fixture.

One known-revision Update attempt stopped at 1,522 copied files: npfs reported a
background failure and VirtIO timed out/retired the device. The UI ended its live
row, printed copy failure and recovery guidance, and exited 1 without replacing
the ESP. Its log/capture are retained as `update-timeout.*`. This is an observed
storage failure, not a proven reporting defect or a throughput result.
The clean-fixture retry completed: 2,231 files / 47,429,393 bytes copied and
compared, raw EFI verification 5/5, and final `updated`. No debugger was attached
to the successful retry. No causal claim is made about the earlier timeout.

A separate legacy fixture kept the baseline GPT/normal-rescue ESP but replaced
its npfs pool using the existing host formatter: bin contained a single
15-byte program in the recorded revision, with system/home volumes and no
revision manifest. Both engines' host checkers accepted it. Update re-showed
selected disk identity directly above the `scan` question; after read-only
hashing, Review stayed visible with 56 new roots and one removed root. The full
raw-disk SHA-256 was identical before/after cancellation. This checks the
prompt transition on a small fixture, not a long inventory throughput claim.

The `-v` path displayed local detail and per-root old/new values. Its QEMU input
fixture compiled the trusted init-install source with only its argv
set to `-v`, then replaced that one executable in a local initrd/ISO. No fixture
code or boot option is committed; the production source and normal qualification
ISO were not modified by this fixture.
The verbose Print action emitted `pyxis-install-plan 1` through terminal `end`
and exited 0. Main explicitly writes Review/detail to stderr and the unchanged
canonical serializer to stdout; the shared console capture displays both.

Normal Print also expanded per-root detail on demand, emitted the framed plan
and exited 0. No apply consent was given in either Print run. Non-console sinks,
narrow/resized consoles, clock/diagnostic-output failure and the standalone
editor’s retained local detail were source-reviewed, not fault-injected or
runtime-qualified here. No native media was touched and no performance comparison
is claimed.

### Unified environment integration

During the final freshness check, main advanced to `0f1aa2f8` (unified process
environment), userland `f26690d` and ports `2c49e86c`. The task was linearly
rebased: diagnostic bootstrap uses the current borrowed stdio binding, owns a
WRITE-only/transport=0 copy and declares a private ENV capability; the installer
captures that explicit name. The configuration editor still has no diagnostic
writer. Taking the one-shot space factory unpublishes its original ENV entry,
so closing its local owner before wait preserves the existing retirement contract.
No libc, shell, public ABI or port changes were introduced by this task.

The rebased image at parent `81f506f0`, userland `499fca0`, rebuilt warning-free
using matching main SDK/ports artifacts (Actions API run 2537) with the same
builder. Earlier functional captures remain the pre-environment record above.
Both rebased flows completed on separate disposable disks: 2,231 files /
47,603,873 bytes copied and compared, raw EFI verification 5/5, final `installed`
and `updated`. The known-revision Update correctly reported all 56 roots updated
by the runtime migration (47,426,689 → 47,603,873 bytes). Normal Review and final
progress stayed within the 1280×800 view. Private diagnostics appeared in serial;
neither rebased run reported a storage timeout. Captures/logs are under local
`build/qualification/environment`.

The final parent rebase also incorporates main `dd0e608c` (vela boot changes),
without changing installer code or userland `499fca0`. SDK/public headers and
runtime inputs remain the same; the full image and read-only Review are checked
again after that rebase. The normal/verbose Print and small legacy fixture
checks above precede the environment migration; their rendering/serializer code
is unchanged. Observer failure paths remain source-only evidence.
