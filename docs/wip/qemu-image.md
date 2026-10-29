# Ready-to-run QEMU image

**Plan accepted in part (chronium, 2026-10-26 and 2026-10-29).** Each release
publishes a ready-to-run qcow2 beside the install ISO, with that release already
installed, so a tester needs QEMU and OVMF but no installation. Motivation: an
outside tester struggled with QEMU setup (the `kvm` group, `sudo`, OVMF). It
ships with 0.0.10 if ready, otherwise 0.0.11, and it never blocks a release.

Accepted:

- **Virtual size 32 GiB, sparse** (changed from 16 GiB on 2026-10-29).
- **A genuine Classic install.** Built at release time from the tag by running the
  real installer inside QEMU, so `pkg upgrade` and release upgrades work from it.
- **One shared identity.** Every download carries the same disk GUIDs, pool
  identity, install IDs and hostname. It boots with stock OVMF.
- **Built as part of the release flow, not as a later addendum** (2026-10-29): the
  build takes about three minutes, small next to the release's Clang build. A
  failure never blocks the release: the ISO and repositories publish as usual and
  the failure is reported.
- **A copy-paste launcher in the manual**, not a downloaded script (2026-10-29).
- **Worker changes** are deployed by the orchestrator after chronium approves each
  one (2026-10-29).

Proposed and not yet answered: driving the installer by QEMU key injection
(decision 1). The implementation PR says so, for chronium to object.

## Findings

Measured on the published 0.0.9 ISO, not 0.0.10: a blank 32 GiB sparse disk,
QEMU 10.2.2, q35, KVM, four CPUs, 4 GiB, user networking, Fedora 44 edk2 OVMF, on
one VM. `qemu-img` was not installed there, so the compressed size is estimated,
and the first real build confirms it.

| What | Result |
| --- | --- |
| Install, `wipe` to `installed` | about 87 s; the action list is up about 25 s after QEMU starts |
| Total guest writes (QEMU block statistics) | 115 MiB in 5,928 writes |
| Allocated on the sparse file | 106 MiB (1,702 non-zero 64 KiB clusters, the last being the backup GPT) |
| Estimated qcow2 with `-c` (deflate per 64 KiB cluster) | about 40 MiB |
| Fresh boot of the installed disk, no ISO, fresh OVMF variables | shell about 50 s after QEMU starts |
| `pkg upgrade` on it | `pkg: 0.0.9 is already running`, exit status 1 |

Nothing writes across the 32 GiB. Install zeroes only the ESP's reserved/FAT
area, and writes the pool bitmap (about 1 MiB), the volume table, two root
blocks, two journal control blocks and the headers. The journal (252 MiB at this
size) is reserved, not written. The whole-disk zero scan is NVMe-only.

The stock ISO has no non-interactive path: `installer-init` launches the installer
without `--confirm`, a blank disk's plan is "interactive apply only", and the
install space has no serial input. `--apply --confirm=wipe` is reachable only by a
trusted launcher that replaces `installer-init`, which would put that init into
the installed core archive.

## Decisions

1. **Drive the installer** by typing its answers through QEMU's QMP `send-key`
   into the unmodified release ISO. Proposed. The sequence on a blank disk is
   Install, Classic, Blank or marked disks, the default journal size, Apply,
   `wipe`, then Restart under `-no-reboot` so QEMU exits after the kernel's flush.
   The driver waits on serial kernel-log markers where they exist and on fixed
   delays elsewhere. A build counts only once verification passes. If this proves
   flaky, stop and discuss an unattended installer mode or a host-built image;
   do not add retries.
2. **One script,** `tools/qemu-image.py` (standard-library Python), with
   `build ISO OUT.qcow2` and `verify IMAGE`. It needs `qemu-system-x86_64`,
   `qemu-img`, OVMF and `/dev/kvm`, no root. No Make target, because the build
   needs the live repository.
3. **Release sequencing.** The image needs the signed repositories live, and
   `sign-images.sh` runs before uploads today. So in the release flow:
   1. sign and upload `public` and `releases` as now;
   2. build the image from the ISO that `release-image` staged (the script
      refuses unless its checksum matches the staged `SHA256SUMS` line), against
      the live `public` repository;
   3. if it built and verified, add `pyxis-VERSION.qcow2` to `SHA256SUMS`; if it
      failed, leave it out and report;
   4. run `sign-images.sh`, then upload the ISO, the qcow2 if any, `SHA256SUMS`
      and its signature last.

   The release is delayed by one image build, not blocked by it.
4. **Size and journal.** 32 GiB sparse; the journal stays at the installer's
   default (252 MiB at this size). The script does not override it.
5. **Compression.** `qemu-img convert -c -O qcow2` (zlib, default clusters) from a
   raw sparse intermediate written with `detect-zeroes=unmap`. Not zstd: it needs
   QEMU 5.1 to read.
6. **Identity.** Install draws its GUIDs and IDs from the guest's random source,
   so each build is a new identity. Build once per release and never rebuild or
   re-publish a name; a failed attempt gets a new build. Whether the hostname is
   shared by construction is not yet checked.
7. **Names and signing.** `pyxis-VERSION.qcow2` in `images/`, in the same signed
   `SHA256SUMS`, so the manual's verification steps work unchanged with
   `--ignore-missing`. Changes: `SUMS_LINE` in `tools/pxpkg.py`, the line check
   and the file loop in `tools/sign-images.sh`, and the `/images/` allow-list in
   `hosting/worker/worker.js` (`/images/pyxis-*.qcow2`, one-year immutable cache
   as the ISO). The Worker change deploys before the first upload, or the file
   answers 404.
8. **User command line.** A copy-paste launcher in the manual's install page: the
   existing launcher with the disk swapped for the qcow2. It picks the first
   OVMF pair that exists from the known locations (Fedora, Debian and Ubuntu,
   Arch), uses `-accel kvm` when `/dev/kvm` is usable and otherwise falls back to
   `-accel tcg` with a note, and copies the OVMF variables per VM. The page says
   to verify the download before first boot, because a boot modifies the image,
   and shows `-snapshot` for a throwaway run.
9. **Left as the installer leaves it.** `system://SAFE_TO_WIPE` and the Classic
   marker stay, so the manual's install-again path and Update keep working.
10. **Verification** is a checklist the release agent runs, not CI:
    `qemu-img info` and `check`; the manual's signature and checksum steps on the
    published file; a boot of a copy with the documented launcher, fresh OVMF
    variables and no ISO, with `cat boot://os-release` matching the tag;
    `pkg upgrade` printing `VERSION is already running` (there is no dry-run flag);
    `pkg check base`; a second boot; one `-accel tcg` boot per release. A real
    upgrade from the image needs a later release, so the first true test is the
    0.0.11 candidate. A 0.0.9 system cannot `pkg upgrade` to 0.0.10 and uses the
    installer's Update, so the image first helps upgrades from 0.0.10.

## Limits

- Only the Fedora OVMF paths have been exercised; the Debian, Ubuntu and Arch
  paths come from the existing manual table.
- The image holds the `base` set only; other packages are `pkg install`.
- 32 GiB is virtual. `qemu-img convert -O raw` or a non-sparse copy makes a full
  32 GiB file.
- Without KVM the build may take much longer (unmeasured).

## Tasks

- [ ] 1. `tools/qemu-image.py` (`build`, `verify`), run once against the 0.0.10
  candidate ISO on the release builder, with the launcher and manual section.
- [ ] 2. Release tooling: `SUMS_LINE`, `sign-images.sh`, the Worker allow-list
  diff for the orchestrator to approve and deploy, and the release-process step
  with its failure rule.
