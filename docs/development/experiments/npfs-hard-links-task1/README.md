# Native hard links: format and host-tool foundation

2026-10-19, task 1 of [the milestone](../../../wip/npfs-hard-links.md).
Baseline fs `25337999c2564ff1154e1c9dbd4f7552ad8f15f3`; candidate fs
`b3aae99f2aec02675edfe2ce0b076e1a2822314e`
([fs #43](https://git.internal/PyxisOS/pyxis-fs/pulls/43)).
This task changes shared codecs and Linux tools; it does not add native LINK,
first-use activation or count-maintaining native mutations.

## Reproduction inputs

Host Fedora x86-64, ordinary `make -j16` with optional libfuse3 available.
The baseline binaries were preserved before changing source. Both versions use
32 MiB disposable pool images, a 1 MiB journal, volume `home`, two independent
regular files and an empty child directory. `file` contains
`hard-link foundation\n`. No owner pool or physical device is used.

```sh
make -j16
build/mkfs.npfs --image /tmp/legacy.raw --size 32MiB --journal 1MiB \
  --volume home --source /path/to/source
build/mkfs.npfs --image /tmp/counted.raw --size 32MiB --journal 1MiB \
  --volume home --source /path/to/source --hard-links
build/fsck.npfs --image /tmp/counted.raw
build/npfs-inspect --image /tmp/counted.raw stat --volume home --path file
```

`/shared/present/npfs-hard-links-task1/` holds `fixtures.py` (scratch image
construction), `checks.py` (recorded existing-tool invocations), `host-checks.log`,
`fuse-check.py`/`.log`, `host.gdb`, `host-gdb.log` and the build logs. Scripts are
qualification records outside the repository, not new shipped test infrastructure.
Fixture images are disposable. Recreate them with the recorded source and tools;
`fixtures.py` starts from the baseline-formatted `base.raw`. `checks.py` mutates
only the explicit writable replay/repair copies; regenerate before another run.

## Host observations

| Check | Baseline | Candidate |
| --- | --- | --- |
| Legacy image and ignored reserve bytes | Ordinary legacy check passed | Passed, effective count one |
| Counted aliases | Rejected multiple names | Passed; both names report inode 2, links 2 and identical bytes |
| Unequal valid headers | Refused | Selects strict componentwise feature superset |
| Writable counted opening | Unsupported | Audited host replay/repair admitted |
| Conservative shared write admission | RO features refused | Unchanged: current kernel still admits no RO-feature writes |

Candidate checks also rejected count too small/large, aliases without the bit,
directory aliases, a linked DETACHED inode, nonzero FREE/directory counts and
cross-inode block sharing. The checker still walks each inode's mapping once.
Untouched stored-zero legacy files remain effective count one after header gain.

Read-only checks preserved whole-image SHA-256. Either header could be the
superset. Writable `--replay` repaired the valid subset byte-for-byte, including
ignored reserved bytes. Changed IDs/reserves and crossed masks refused opening;
unknown required features refused all opening, and unknown RO features refused
writable opening without changing the image. A committed pool's ordinary opening
refused before repairing its lagging header. One-valid-copy admission remained
available with a warning, without repairing that invalid copy.

A valid two-image committed redo added the alias through directory/inode metadata.
Writable replay and subsequent fsck passed. A checksum-valid redo with an invalid
count on a directory inode refused before writes. Read-only FUSE presented both
ordinary and RAM-replayed aliases with equal Linux inode identity, `st_nlink=2`
and matching bytes; both source hashes remained unchanged. FUSE was unmounted and
its exact process stopped after each check. Host GDB observed the selected
RO-compatible mask 1 and effective file count 2 during the checker traversal.
A further host GDB check on the same header observed default writable admission
return UNSUPPORTED while explicit host support returned OK. Inspector decoded
`UINT32_MAX` without narrowing; valid but changed geometry was refused by header
selection. These do not qualify native overflow-before-effects admission.

## Integration and limits

An ordinary target kernel build passed with the published compiler image
`git.internal/pyxisos/pyxis-builder:pyxis-llvm23.1.3-3d69e83`; no compiler rebuild.
Parent CI3160 at `bf9ae1f3e6d4ad94ef80194686f225be3af0f616` passed all four jobs,
including the full image/SDK build. Integration used that run's exact kernel
and archive (artifact 13778), with only a private installed-space configuration
and `qualification.lua` added to the archive. Hashes and launch arguments are in
`qemu-SHA256SUMS` and `qemu/command.json` under the record directory.

Interactive QEMU used q35/KVM, four CPUs, 4 GiB, OVMF, modern VirtIO net/block
and a disposable 34 MiB GPT disk containing a 32 MiB legacy `bench` pool.
The Remote client ran:

```text
boot://lua.pxb boot://qualification.lua
boot://sync.pxe data://
exit
```

Lua verified the original file, created/read a file, renamed and removed it;
its PASS marker and both program exits were zero with complete terminal drain.
The initial attempt named absent `lua.pxe`; the correct archive tool is `lua.pxb`.
GDB inspected all four CPUs and the writable legacy pool: RO feature mask zero,
failed=false, staged images zero and selected journal EMPTY after sync.
After quitting that exact QEMU PID, extracted-pool fsck passed and the original
file still matched its source, reporting effective count one. No target-side
injection or kernel behavior change was needed. QEMU/GDB/client jobs were stopped.

The parent pin exports the same updated codec to kernel/SDK and host tools.
The current kernel's opening and writable-upgrade paths still call conservative
`npfs_features_check`, so this pin cannot enable count-unaware writes to linked
pools. Native unequal-header selection/repair, LINK, activation and all count
mutations remain task 2; the existing native opener remains conservative meanwhile.

No performance change or native-media result is claimed. Native LINK authority,
undo, last-name/held-handle cleanup, shrink, count overflow admission, interrupted
activation and crash/power-loss durability require the later writer tasks. No
formatter source-alias preservation, fsck guessed repair or symbolic links.
