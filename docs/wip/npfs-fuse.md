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

The [host tools](../../fs/docs/npfs-host-tools.md) read pools through the shared
format library. `npfs-fuse` mounts regular pool images and npfs partition devices
read-only, with one top-level directory per volume. It validates committed
journals and retains their metadata images in RAM for the mount's lifetime.
The source must remain unchanged throughout; regular images keep a shared lock,
while devices have no external writer exclusion. Other tools retain their
image-only contract and refuse COMMITTED without writable fsck replay.

There is no GPT selection, automatic mounting or host writer. The runtime writer
belongs to Caelum. The owner's physical ThinkPad-stick mount/copy remains pending.
The published builder with libfuse3 has been pulled and its host build qualified.

## Decisions

Accepted by the owner on 2026-10-06:

1. **Scope:** read-only first. A manual command mounts a partition device or an
   image file, with one top-level directory per volume. Automatic mounting and
   writing are separate follow-ups.
2. **Committed journal:** task 1 refused committed transactions. Task 2 replaces
   that refusal in FUSE with a validated read-only RAM view. Writable fsck and
   RAM replay share payload integrity validation; RAM replay retains read-only
   feature admission and does not increment the sequence or clear the source
   control. Invalid payloads or memory exhaustion fail before publishing a view.
3. **Build:** an optional `npfs-fuse` target, built only when the libfuse3
   development package is present. The implementing agent may also add that
   package to `ci/Containerfile` in pyxis-os. The owner then rebuilds and
   publishes the builder container, and the existing fs CI job (`make -j16`)
   builds the target.
4. **Linux timestamps:** accepted 2026-10-06. Preserve creation/modification
   times as read-only `user.npfs.created_ns` and `user.npfs.modified_ns` xattrs,
   containing signed nanoseconds or `unknown`. Linux mtime uses modification
   time, with zero as its documented fallback when unknown. Since npfs has no
   access or POSIX change timestamp, Linux atime/ctime use the same mtime.

## Tasks

- [ ] **1. Read-only mount.**
  Implementation and QEMU qualification are recorded in the
  [task-1 validation](../development/experiments/npfs-fuse-task1/README.md).
  The checkbox remains open until the owner mounts the ThinkPad stick and copies
  a file; that physical step cannot be substituted by emulation.
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
    - the initial version refuses a committed journal with the recovery message
      (qualified in task 1, superseded by task 2);
    - the source bytes are unchanged after unmounting;
    - the owner mounts the ThinkPad stick and copies a file from `system://`.

- [x] **2. In-memory journal replay.**
  Implementation, current-main QEMU interruption, read-only partition access and
  the merged review follow-ups are recorded in the
  [task-2 validation](../development/experiments/npfs-fuse-task2/README.md).
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
