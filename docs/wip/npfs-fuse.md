# Mounting Pyxis volumes on Linux

Status: **milestone, agreed 2026-10-06.** This is host tooling in
[pyxis-fs](../../fs/README.md). It runs in parallel with the
[system layout](system-layout.md) and changes no kernel or userland code. Each
task starts when the owner says so.

## Goal

Plug the Pyxis USB stick into a Linux machine and read its volumes as ordinary
directories. For example, copy work developed on the ThinkPad into a git
checkout, or recover files from a stick that will not boot. The mount has no
concept of spaces: it shows every volume in the pool.

## Today

The [host tools](../../fs/docs/npfs-host-tools.md) already read pools through the
shared format library. `npfs-inspect` lists volumes and directories, stats and
prints files, and extracts single files. Three gaps remain:

- the tools open only regular image files, not partition devices or GPT
  selection;
- journal replay exists only as writing recovery (`fsck.npfs --replay`), with no
  read-only replay held in memory;
- the runtime writer belongs to Caelum, so there is no host writer.

## Decisions

Accepted by the owner on 2026-10-06:

1. **Scope:** read-only first. A manual command mounts a partition device or an
   image file, with one top-level directory per volume. Automatic mounting and
   writing are separate follow-ups.
2. **Committed journal:** the first version refuses a pool whose journal holds a
   committed transaction, and says how to recover: boot Pyxis once, or run
   `fsck.npfs --replay` on a copy of the image. Replaying in memory is the next
   task.
3. **Build:** an optional `npfs-fuse` target, built only when the libfuse3
   development package is present. The implementing agent may also add that
   package to `ci/Containerfile` in pyxis-os. The owner then rebuilds and
   publishes the builder container, and the existing fs CI job (`make -j16`)
   builds the target.

## Tasks

- [ ] **1. Read-only mount.**
  - `npfs-fuse SOURCE MOUNTPOINT`, where SOURCE is an npfs partition device, for
    example `/dev/sdb2`, or a pool image. The device is opened read-only. Other
    tools keep their image-only contract.
  - Each volume appears as a top-level directory named after it, for example
    `system/`.
  - Pyxis objects have no permission bits, so entries are owned by the mounting
    user with read-only modes. Creation and modification times map directly,
    and unknown times are shown as such.
  - Opening follows the host tools: both headers, the features and the control
    selection are validated; unknown required features refuse the mount; image
    files take the shared `flock`.
  - **Finish when:**
    - a pool image made by Pyxis in QEMU, on RAM-backed storage, mounts on
      Linux, and its files match their SHA-256 sums taken inside Pyxis;
    - a pool with a committed journal is refused with the recovery message;
    - the source bytes are unchanged after unmounting;
    - the owner mounts the ThinkPad stick and copies a file from `system://`.

- [ ] **2. In-memory journal replay.**
  - A committed journal is validated as `fsck.npfs --replay` does, then applied
    to an in-memory view of the affected blocks. Nothing is written to the
    source.
  - **Finish when:** a pool image left with a committed journal by an
    interrupted Pyxis write mounts with the replayed contents, and the source
    bytes are unchanged afterwards.

## Later

- **Automatic mounting** on plug-in, for example a udev rule matching the npfs
  partition.
- **Writing.** This needs its own decision between sharing Caelum's writer as a
  library for both kernel and host, and a separate host writer checked for
  agreement with `fsck.npfs`. Either is milestone-sized.
- **Permissions** wait for the [users milestone](users-and-authority.md); for now
  everything is visible to the mounting user.
