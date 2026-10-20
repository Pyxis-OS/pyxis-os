# Release plans and ideas (0.x)

Pyxis has reached the point of being shaped into an OS. 0.1.0 is about
integration: closing the gaps between pieces that already work separately, because
those gaps are what make a first tester's experience worse. For example, `pkg`
works, but installing or updating needs a separate boot entry; games exist, but
their data is separate. 0.1.0 does not need a desktop environment or compositor.

Status: **collection, started by the owner on 2026-10-20.** This list gathers
ideas for 0.1.0 and for later releases (0.1.n, 0.2 and so on). Nothing here
authorizes implementation: an entry becomes a milestone task only when the owner
assigns it, and its decisions are settled in its own document first.

Each entry carries three fields:

- **Status:**
  - **idea**: raised, not decided.
  - **accepted direction**: the owner accepted the direction and its listed
    decisions; nothing is scheduled by that alone.
  - **scheduled**: the owner placed it in the [milestone index](boot-sdk-ports.md).
- **Target:** 0.1.0, or later.
- **Covered by:** the release that landed it, or none yet.

An entry is not tied to the tag it targets. It may land in any release (0.0.8,
0.0.9 and so on), and when it lands it records which release covered it. 0.1.0 is
ready once every entry targeted at 0.1.0 is covered by an existing release and no
new 0.1.0 ideas are coming (chronium, 2026-10-20).

Entries are numbered in the order they were raised. There are no dates or
estimates.

## 1. Disk management: the live image is the installer

Status: **accepted direction (owner, 2026-10-20); not implemented.**
Target: 0.1.0. Covered by: none yet.

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
- names always decoded, with no raw hex or UTF-16 dumps;
- disk and partition lists identify a disk the way a person recognises it: size,
  model, transport (USB, NVMe), and decoded partition and volume names; never an
  ID alone (owner, 2026-10-20, after the 0.0.7 installer showed only IDs at disk
  selection);
- the plan review is written for a person: a short summary first (target disk as
  above, what is kept, what is replaced or created, sizes in human units), then
  clearly separated sections with spacing; raw values a person would not
  understand (offsets, sector counts, hashes, internal IDs) stay out of the review
  and go to the detail log or an explicit print-details action (owner, 2026-10-20:
  the 0.0.7 installer/updater plan dump is impossible to parse and must not be
  copied into the new tools).

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

Status: **accepted direction (owner, 2026-10-20); not implemented.**
Target: 0.1.0. Covered by: none yet.

An installed system updates itself through `pkg`, with no separate boot entry or
update path. The release is a package; `pkg` writes it to the ESP of the install
that booted. Direction, trust rules and schedule are in the
[upgrader section](packages.md#upgrader-pkg-upgrade-accepted-direction-owner-2026-10-20)
of the packages plan. The [details](pkg-upgrade.md) and their three decisions are accepted
(owner, 2026-10-20). Entry 1 builds its install layer on the same publisher.

## 3. Game packages: program and data together

Status: **accepted direction (owner, 2026-10-20); not implemented.**
Target: 0.1.0. Covered by: none yet.

`doom` and `quake` become whole packages, program plus data, with no separate data
packages. Today the programs ship in the image and require their data packages.
The accepted defaults are in the packages plan under
[game packages](packages.md#the-boot-archive), and the temporary exception is
recorded as [game programs in the image](../technical-debt.md#game-programs-in-the-image).

## 4. Personal programs by name

Status: **idea; no design yet.**
Target: 0.1.0. Covered by: none yet.

A program copied to `home://bin` runs only by its full path, and Tab does not offer
it, because bare names resolve only through the held command list. The noted gap is
the *Personal command directory* row in [everyday gaps](everyday-gaps.md), and the
current rule is in [command search](../userland/command-search.md).

## 5. The image is assembled from packages

Status: **accepted direction (owner, 2026-10-20); not implemented.**
Target: later. Covered by: none yet.


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

## 6. Space editor

Status: **idea; no design yet.**
Target: later. Covered by: none yet.


A small curses tool that edits spaces graphically: pick a space, toggle its
true/false settings with checkboxes (`network`, `focus`, `launch`, `jobs`,
`screenshot`, `power`, the clipboards, `package_management` and the rest), and
edit its grants and roots. It would follow the input convention of the other
curses tools in this document: lists, Enter as the default, and typed words only
for destructive confirmations (see the typed confirmation in entry 1).

What exists today:

- Spaces are configured in `boot.lua`; the shape is in the shipped
  `userspace/config/installed.lua`, and the loading rules are in
  [init](../userland/init.md).
- The installer's configuration delta editor (`userspace/config-editor/`, run as
  `config edit`) already edits declared grants with libterm prompts and validates
  the result; it is the editing half of Polaris, per the
  [installer plan](installer-tui.md#configuration-checker) and
  [Asterism](spaces.md#asterism). See [configuration editor](../userland/configuration-editor.md).

The space editor would turn today's manual edit, adding `package_management = true`
and the packages grant to a space so that `pkg` can manage the
[packages volume](../interfaces/packages.md), into a checkbox.

## 7. Shell prompt and last exit status

Status: **idea; no design yet (chronium, 2026-10-20).**
Target: 0.1.0. Covered by: none yet.

### Today

The prompt shows only the working path, for example `tmp://notes> `, or
`[cwd unavailable]` when the path cannot be described. It has no colour, no
hostname and no sign of how the last command ended. The [shell](../userland/shell.md)
documents no variable holding the last command's exit status. The only
expansions it describes are the script arguments (`$1`, `$#`, `"$@"`) and
`$(...)`. Exit statuses drive `&&` and `||` and the status of `-c` and scripts,
but a script cannot read the previous command's status and the prompt cannot show
it.

### Wanted

Like chronium's oh-my-zsh prompt on horse:

- colours in the prompt;
- a variable holding the last exit status, so scripts and the prompt can both use
  it;
- a small arrow at the start of the prompt, green after exit 0 and red after a
  non-zero status;
- the hostname, which Pyxis already keeps as the
  [machine hostname setting](../userland/machine-settings.md#hostname-schema).

### Fit with what exists

The [startup settings](../userland/shell.md#startup-settings)
(`home://.config/shell/startup.lua`) are strict literal data with text values
only, never run as code. They are a likely place for a declarative prompt
description, rather than executable prompt code. Colours would use the `TERM=pyxis`
terminal profile (see the [terminal](../userland/terminal.md) SGR support).

### Open questions

- The name of the last-status variable.
- The prompt format language: what a declarative description can express.
- The colour palette, and whether it is a theme.

## 8. Spaces on the fly

Status: **idea; no design yet (chronium, 2026-10-20).**
Target: later. Covered by: none yet.

With Polaris (see [Asterism](spaces.md#asterism)), creating a space should be as
easy as editing one. For example, spawning a managed space that hosts an S3
server for a quick development run:

- it holds only the minimum rights and grants its job needs;
- it runs on a ramdisk or on a storage-backed volume;
- it can be removed when the work is done.

It relates to the Asterism pieces that do not exist yet: Continuum, the
supervisor in every space, and a haven, a space without a terminal managed
exclusively by Continuum, which is what a server like this would be. Asterism's
runtime configuration (creating and destroying spaces while the system runs)
already names the lifecycle. It also relates to entry
[6, the space editor](#6-space-editor), which edits an existing space's settings,
grants and roots.

Design constraint it implies now: space configuration stays live-editable and
composable. An ordinary edit takes effect without a separate publish or apply
step. The Asterism text describes Polaris as applying a change to Continuum as a
migration from the running configuration; this entry asks that such a step not be
required for ordinary edits.

Examples (chronium, 2026-10-20):

- Different spaces on weekdays and weekends. Later, through a sandboxed computed
  configuration with a read-only clock capability, evaluated once per boot;
  `boot.lua` is declarative only for now.
- A specific external drive or stick present at boot spawns its own space, with
  its own configuration and init, holding only that volume.
- A tool unlocks an encrypted volume only inside its own space, using a credential
  from a credential manager (not designed yet), so no other space can read it.
