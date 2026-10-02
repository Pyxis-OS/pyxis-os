# Simple native filesystem

Status: **owner direction, 2026-10-02.** This restarts writable filesystem
work. It supersedes the pyxis-fs writer plans listed under
[what stops](#what-stops). Only the [owner decisions](#owner-decisions) are
accepted; the [first questions](#first-questions) and the
[proposed working method](#proposed-working-method) await the owner.

## Owner decisions

1. **Start again with a deliberately simple filesystem.** Aim for ext2-class
   structure, not a complete design. Pyxis has no users yet, and it doesn't need
   to know what it will look like in a week. Simple structures also keep writes
   and latency low. New capability is added later through reserved bytes and
   feature flags in the on-disk structures, which the format must carry from the
   start.
2. **Native to Caelum.** The kernel owns mount state, caching and the writer,
   and uses Caelum's memory management. Persistent in-memory state between calls
   is expected, not avoided.
3. **The shared library covers the format only.** It holds on-disk structure
   definitions, encoding and decoding, and perhaps small allocation helpers. It is
   used by host tools (`mkfs`, `fsck`, inspection) and by Caelum. Whatever the
   library needs from its environment is an external symbol that the host or
   Caelum defines at link time. There are no function-pointer callback tables.
4. **No users or permissions in the filesystem for now.** Access control stays
   with capabilities. A permission model waits until Pyxis boots from its own
   filesystem and there is a concrete need.
5. **Wear budget: no faster than one SSD every six months.** For the ThinkPad's
   256 GB SK hynix NVMe, SMART reports 17.38 TB written at 2% used. Because that
   percentage is a whole number, it implies roughly 580–870 TB of endurance, so
   six months allows at least about 3.2 TB per day. Recompute from the device's
   own SMART data rather than treating this figure as fixed. See the
   [T14 inventory](../targets/t14-gen1-amd/notes.md).
6. **Latency and bytes written are measured from the first implementation,**
   because the previous design failed on latency first.

## Why

The pyxis-fs writer was designed as a portable, stateless library. Each
publication reloads and validates the whole allocation map, then regenerates the
complete candidate. In the matched RAM matrix, a durable 4 KiB call took 13–80 ms,
against 0.12–0.16 ms for Btrfs, and grew 3–5x from 1 MiB to 20 MiB of stored data.
That is the [overflow-split record](../../fs/docs/overflow-split-measurements.md)
at filesystem `a250731`. Write volume was already below Btrfs, but at that
latency every synchronous write would stall the system.

The accumulated plans also no longer reliably record the owner's intent. Some
were accepted in bulk during long decision sessions. Rescoping them would cost
about as much as starting over.

## What stops

- No further pyxis-fs writer work: no write-efficiency, latency or map-planner
  tasks, and no further measurement matrices. These plans are superseded and kept
  only as history:
  - [writable filesystem core](writable-filesystem-core.md);
  - [write efficiency](filesystem-write-efficiency.md);
  - [retirement debt](filesystem-retirement-debt.md);
  - [overflow split](filesystem-overflow-split.md).

  The owner decides separately whether to delete them now or when the
  replacement lands.
- The pyxis-fs writer is never run against a real disk.
- The [native read-only mounts](../devices/native-readonly-filesystem.md) keep
  working on the current format until the new filesystem replaces them.

## First questions

At most three at a time, each with a proposed default. "Defer" is a valid
answer.

1. **What does a completed write promise?** Proposed default: data is durable
   after an explicit sync or close. A crash may lose unsynced data but never leaves
   inconsistent metadata. The alternative is every call durable, as before, which
   costs a flush per call.
2. **How are crashes handled?** Proposed default: a metadata journal with ordered
   data, as in ext3/ext4. A journal record plus a commit is a few blocks per sync,
   and nothing in it grows with the filesystem's size. The alternative is ext2's
   approach: no journal, and run `fsck` after an unclean shutdown.
3. **What is v1 for?** Proposed default: Pyxis boots from it as its root
   filesystem on the ThinkPad. That needs regular files, directories, block
   bitmaps and an inode table, and nothing else: no links, permissions, quotas,
   snapshots or retained history.

## Proposed working method

- The owner states requirements first, in short form. Agents work from them and
  never expand them on their own initiative.
- Decisions go at most three per round, each with a default, and any can be
  deferred. Agent proposals are never "agreed" by default. A decision accepted in
  bulk can be reopened at any time.
- Design documents stay short. A design that needs thousands of lines of
  justification is a signal to simplify it.
