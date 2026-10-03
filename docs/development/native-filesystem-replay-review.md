# Native filesystem replay review

Independent post-merge review supplied by Claude on 2026-10-03, copied from
`/shared/pyxis-native-fs-replay-check.md`. The observations below belong to that
review; they are not a new crash-validation claim by the implementation agent.
The three diagnostics are addressed by the follow-up PRs.

Tested on pyxis-os main `37bd552` (#348 merged), fs `caf8edc`. QEMU 10.2.2, nested
KVM, 4 CPUs, 2 GiB. A 1 GiB pool with a 16 MiB journal in a GPT disk on tmpfs,
already holding 7×100 MB files. Mounted `--read-write data://` from init, with
commands sent over the remote terminal.

**No blockers.** Mount-time journal replay works. Earlier crash tests never reached
it, because the background checkpoint always finished before the kill.

## What was run

1. **Committed journal on disk.** I built a local-only kernel, never committed,
   whose `native_store_maintain` returns early instead of checkpointing a
   COMMITTED journal. On it I ran:
   - `mkdir data://replay`;
   - a 1 MiB `replay/a.bin`, then `sync`;
   - `replay/b.txt` (`date`), then `sync`;
   - then `kill -9` of QEMU.

   Host `pyxisfs-native-fsck` then refuses with `journal replay required`, as
   designed.
2. **Host replay, on a copy of the partition.** `pyxisfs-native-fsck --replay`
   passes. Both files are present, `a.bin` matches the source byte for byte, and
   `b.txt` holds the timestamp.
3. **Kernel replay at mount, on the original disk, using unmodified main.** `ls`
   shows `a.bin` and `b.txt`, and `b.txt` has the right content. After that:
   - a new 1 MiB `c.bin` with `sync` works;
   - `rm b.txt` with `sync` works.

   After a clean shutdown, host fsck passes, `a.bin` and `c.bin` both match the
   source, and `b.txt` is gone.
4. **Read-only mount of a pool with a committed journal.** The mount is refused
   and init stops at that line. This is the intended outcome, but see item 2
   below.

## Actionable, all small and non-blocking

1. **Replay is silent.** Neither mount nor replay logs anything. A recovered
   unclean shutdown is worth one line on the Caelum tab, for example:

   `nativefs: replayed journal sequence N (M blocks)`

   On the ThinkPad that is the only sign that the last shutdown lost unsynced
   data.
2. **The read-only refusal message is misleading.** The init shell shows
   `shell: mount: data://: Read-only filesystem`. That happens because
   `PNF_RECOVERY_REQUIRED` maps to `CALL_READ_ONLY` in `format_failure`
   (`kernel/fs/native_store.c`), but the user *asked* for read-only. The host
   tools say it correctly: `journal replay required`.

   Suggested default, with no ABI change: a `klog` at that refusal, e.g.
   `nativefs: journal replay required; mount read-write once to recover`.

   Related: `mount --optional` only covers a missing mount authority. Any other
   failure still stops init, so an init that mounts read-only would never reach
   the session or remote terminal. That is the current documented behaviour; I'm
   noting it only so the owner knows the consequence.
3. **Host `pyxisfs-native-inspect` rejects absolute paths without saying why.**
   `list --path /replay` fails with `list: invalid argument`, while
   `--path replay` works. Either accept one leading `/`, or say in the error
   that paths are relative to the volume root.

## Method notes

- The checkpoint-skip kernel was a local working-tree edit, reverted afterwards.
  I'm not suggesting adding fault injection to the repo.
- A remote-terminal session has no native mount authority. A `mount` there
  returns `Working directory or resource unavailable` even on a healthy pool, so
  the read-only case has to be tested from init. To read the init shell's error
  I used a QEMU monitor `screendump`.
- QEMU itself segfaulted once while Limine was loading `caelum.elf`. The retry
  booted normally. That's a host QEMU crash, not Pyxis; I'm recording it only in
  case it recurs.

## Diagnostic follow-up validation

The kernel follow-up adds a refusal explanation while retaining READ_ONLY and a
success line only after replay publishes and flushes the newer EMPTY control.
The inspector follow-up in [pyxis-fs #28](https://git.internal/PyxisOS/pyxis-fs/pulls/28)
retains relative paths and explains that restriction when rejecting a leading
slash. ABI, optional-mount behavior and journal ordering are unchanged.

The implementation agent built the kernel/image, SDK, userland, ports and host
tools using the existing compiler. Kernel source was main `37bd552` plus the two
logging changes; the recovery boots used filesystem `f95e5a3`. This separate
manual check used QEMU 10.2.2, nested KVM, four CPUs, 2 GiB, fresh OVMF variables,
virtio-net/RNG and writable virtio-blk with `cache=writeback`. The disposable disk
held a 128 MiB pool with an 8 MiB journal; it was copied from the earlier task-3
disk into the build directory, rather than reusing the review's populated pool.
This check records behavior, not latency or physical-media qualification.

GDB paused the ordinary kernel at entry to `checkpoint()` after a new
`mkdir data://replay-diagnostics` committed. The selected control was COMMITTED,
sequence 2952, with two metadata images. A stable copy of the disk was taken
while the guest was paused; the original resumed and checkpointed normally.
No kernel edits to skip checkpointing or injected failures were used in this
follow-up. Host read-only fsck of the snapshot required journal replay.

Booting the snapshot from init with `mount --read-only` emitted:

```text
nativefs: journal replay required; mount read-write once to recover
```

Host read-only fsck still required replay afterward. Booting the same snapshot
from init with `mount --read-write` emitted exactly one success line:

```text
nativefs: replayed journal sequence 2952 (2 blocks)
```

The session could list the recovered directory, create a file within it and
synchronize that file. After QEMU stopped, host structural fsck passed. Source
review checked failure-path silence and worker/log ownership. The existing
`docs/development/experiments/native-filesystem-task3/` records are unchanged.
