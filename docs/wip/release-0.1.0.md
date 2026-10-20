# Release 0.1.0 plans and ideas

Pyxis has reached the point of being shaped into an OS. 0.1.0 is about
integration: closing the gaps between pieces that already work separately, because
those gaps are what make a first tester's experience worse. For example, `pkg`
works, but installing or updating needs a separate boot entry; games exist, but
their data is separate. 0.1.0 does not need a desktop environment or compositor.

Status: **collection, started by the owner on 2026-10-20.** Plans and ideas for
0.1.0 are collected here, and the owner adds entries as they come up. Nothing here
authorizes implementation: an entry becomes a milestone task only when the owner
assigns it, and its decisions are settled in its own document first.

An entry is not tied to the 0.1.0 tag. It may land in any release (0.0.8, 0.0.9
and so on). 0.1.0 is ready once every entry has been covered by an existing
release and no new ideas are coming. When an entry lands, it records which
release covered it.

Each entry carries one status:

- **idea**: raised, not decided.
- **accepted direction**: the owner accepted the direction and its listed
  decisions; nothing is scheduled by that alone.
- **scheduled**: the owner placed it in the [milestone index](boot-sdk-ports.md).

Entries are numbered in the order the owner raised them. There are no dates or
estimates.

## 1. Disk management: the live image is the installer

Status: **accepted direction (owner, 2026-10-20); not implemented.** Covered by:
none yet.

### Problem / today

Implemented behavior is in the [native installer](../userland/installer.md) and
[system updates](../userland/system-updates.md).

- Install is a separate boot. The `Install Pyxis` boot-menu entry starts
  `init-install.pxe` with `boot.install=1`.
- Classic install always rebuilds the whole selected disk: a fresh GPT, a 512 MiB
  FAT32 ESP and one pool holding the `system`, `bin`, `home`, `packages` and
  `http-work` volumes.
- Update is a second, special path in the same program, with its own program stage
  and boot-file publication.
- Nothing can be installed beside another OS, for example vela's Fedora on half
  of the NVMe. The Coexist profile exists and appends a pool in a reviewed gap of a
  disk that keeps its partitions ([NVMe coexistence](nvme-coexistence.md)), but
  nothing makes that gap, and native NVMe writes are still disabled
  (`nvme.write=0`).
- Installer and updater output is hard for a human to parse. GPT names appear as
  raw UTF-16 with `\0000` runs, and each step prints long lists of lines. The
  [TUI plan](installer-tui.md) already requires a progress bar rather than a log
  for the copy phases; this entry covers the rest of the output.

### Direction

Three layers, each an ordinary program that works in a live or an installed
session.

1. **The partition manager**, the first curses tool:
   - lists disks and their GPT;
   - creates an ESP (FAT32) and a Pyxis pool partition in free space;
   - shows foreign partitions with decoded names and types, for example "Fedora
     root, ext4, 237.5 GiB".
2. **Pool and volumes:**
   - a new pool is created with the mandatory volumes built in;
   - the user adds volumes at any time;
   - this is a pool operation, valid on a running installed system, with no
     partition change.
3. **Install:**
   - create the ESP and pool (layers 1 and 2), then publish a release with the
     same publisher that [`pkg upgrade`](packages.md#upgrader-pkg-upgrade-accepted-direction-owner-2026-10-20)
     uses, targeting the chosen ESP instead of the booted one;
   - install and upgrade become the same act with a different target;
   - the `Install Pyxis` entry, `init-install` and `boot.install` retire once this
     is qualified.

Readable-output rule for all three:

- one short human line per step, keeping the mandatory progress;
- full detail in a log file, named at the end or on failure;
- names always decoded, with no raw hex or UTF-16 dumps.

### Decisions (accepted, owner, 2026-10-20)

1. **Foreign partitions.** In the first version they are shown and preserved,
   never resized or formatted. Deleting one requires typing its decoded name back.
   Pyxis creates only in free space. Resizing foreign filesystems is a later,
   separate question.
2. **Disk-write authority.**
   - A declared raw-disk grant is held only by the partition manager, the
     installer and `pkg upgrade`.
   - Every write goes plan, then review, then typed confirmation, as the
     installer does today.
   - Anything in use is refused: the booted medium's mounted partitions and the
     running pool's partition.
   - `nvme.write=0` stays the default on live images until this is natively
     qualified, then retires.
3. **Order.**
   - The partition manager first.
   - Then pool and volume management.
   - Then the live installer on the shared publisher, after `pkg upgrade`
     ([packages](packages.md#staged-plan) stage 2).
   - All after 0.0.7.

### Open questions

Not decided.

- Which volumes are mandatory, and whether `http-work` stays one.
- Partition-manager UI details.
- The tester ISO, a live image that installs (relates to 0.0.8).
- Which GPT features stay unsupported.

## 2. `pkg upgrade`: update the running installed system in place

Status: **accepted direction (owner, 2026-10-20); not implemented.** Covered by:
none yet.

An installed system updates itself through `pkg`, with no separate boot entry or
update path. The release is a package; `pkg` writes it to the ESP of the install
that booted. Direction, trust rules and schedule are in the
[upgrader section](packages.md#upgrader-pkg-upgrade-accepted-direction-owner-2026-10-20)
of the packages plan. Entry 1 builds its install layer on the same publisher.

## 3. Game packages: program and data together

Status: **accepted direction (owner, 2026-10-20); not implemented.** Covered by:
none yet.

`doom` and `quake` become whole packages, program plus data, with no separate data
packages. Today the programs ship in the image and require their data packages.
The accepted defaults are in the packages plan under
[game packages](packages.md#the-boot-archive), and the temporary exception is
recorded as [game programs in the image](../technical-debt.md#game-programs-in-the-image).

## 4. Personal programs by name

Status: **idea; no design yet.** Covered by: none yet.

A program copied to `home://bin` runs only by its full path, and Tab does not offer
it, because bare names resolve only through the held command list. The noted gap is
the *Personal command directory* row in [everyday gaps](everyday-gaps.md), and the
current rule is in [command search](../userland/command-search.md).

## 5. The image is assembled from packages

Status: **accepted direction (owner, 2026-10-20); not implemented.** Covered by:
none yet.

### Today

`pyxis-os` pins `userspace` and `ports` as gitlinks and ships a prebuilt boot
archive cpio.

### Direction

- **Packages from CI.** The userland and ports CI publish their programs as
  packages, release-tied for now (see entries [2](#2-pkg-upgrade-update-the-running-installed-system-in-place)
  and [3](#3-game-packages-program-and-data-together)).
- **A release lock file.** `pyxis-os` keeps a small lock file instead of gitlinks,
  listing package IDs, versions and SHA-256 for the release. It gives the same
  reproducibility, lockstep ABI changes and bisectability. An ABI change is a
  kernel PR, then userland publishes against it, then a lock-file bump PR.
- **A generated boot archive.** The archive stops being a shipped artifact and is
  generated from the installed package set: on the host for live images, and on
  the machine at install and at `pkg upgrade` (fetch and verify the packages,
  generate the cpio, publish the kernel and cpio through the shared publisher).
  The archive's minimum, which is boot init, the shell, the rescue set and `pkg`,
  is pinned by the lock file.

### Decisions (accepted, owner, 2026-10-20)

1. **Deterministic generation.** The same package set yields identical archive
   bytes: sorted entries, fixed timestamps and owners, no machine noise. The
   archive hash is recorded in the ESP catalog at publication, so later signing
   or attestation can regenerate it bit for bit.
2. **Rescue stays immutable.** It is generated once at install and never
   regenerated by `pkg upgrade`. Refreshing it is a separate, explicit action.
3. **One generator.** A small C program built for both the host and Pyxis.

### Prerequisites

- Program packages per release (packages [stage 2](packages.md#staged-plan)).
- A userland and ports CI job that publishes packages.
- Signing, before the build trusts packages from outside the LAN.
