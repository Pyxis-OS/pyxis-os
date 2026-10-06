# Read-only RAM journal replay qualification

Manual validation on 2026-10-06 for [task 2](../../../wip/npfs-fuse.md), including
the merged filesystem #30 / Pyxis #437 review follow-ups. Task 2 is complete;
task 1's physical ThinkPad-stick mount/copy remains an owner action. The rebuilt
builder has been published and its optional FUSE compilation qualified below.

## Revisions and environment

Filesystem baseline `77a8c33` is merged task 1. Implementation `375b569` adds the
shared journal loader, RAM overlay and lookup change; published `b427df2` adds
only documentation since that code revision. Parent integration pins the latter.
The guest used current Pyxis main `7245570f8c76`, userland `c3d5668`, ports
`0ead472` and the existing installed-init program. No kernel, ABI, format or target
userland change is required for RAM replay.

Host Fedora 44, Linux `6.19.10-300.fc44.x86_64`, host GCC 16.2.1, cross GCC
16.2.0, libfuse3 3.18.3. QEMU 10.2.2 with the existing AHCI correction, q35
nested KVM, `-cpu max`, four CPUs, 2 GiB RAM, OVMF CODE and fresh VARS, modern
VirtIO block with 512-byte sectors/writeback cache, and VirtIO RNG. The guest
disabled xHCI and retained the 30-second npfs background flush interval. Its
2 GiB disposable sparse disk was RAM-backed under `/dev/shm`.

Standalone `make -j16`, parent `make -j16 fs-tools` and an ordinary source image
build passed. A fresh image build initially hit the host's `/tmp` quota while
unpacking a vendor dependency; its incomplete output was archived, and the image
was built in the existing current-main worktree with its completed build cache.
No prebuilt-bundle substitution was used. Image assembly used the existing
`SPACES=pyxis=boot://init-installed` and
`MOUNT_DISK=6faa2e3f-2f4d-4b9b-9265-17fd889cd402` settings. The validation-only
configuration was restored afterward. A separate `make -j16 PKG_CONFIG=false`
build passed for ordinary host tools without libfuse. Host builds use `-Werror`.
No tests, committed fixtures, fault hooks or CI/boot automation were added.

The newly published
`git.internal/pyxisos/pyxis-builder:pyxis-gcc16.2-binutils2.47` was freshly pulled
at digest `sha256:e9af03bd0bd0ffb5e31651d1e864d6b42a157009fd948f105c7331afb5bf0ec4`.
It reports libfuse3 3.17.2, cross GCC 16.2.0 and Binutils 2.47.20260726. A
network-disabled container with the published filesystem source mounted read-only
and a separate writable output directory passed `make -j16`, including
`npfs-fuse`; its linked libfuse library resolved. This confirms the optional target
build in the updated builder, not a FUSE mount inside the container.

## Interrupted current-main write

The target started from task 1's synced installation. Interactive guest commands
reported the immutable source hash, copied a file, then requested durability:

```text
sha256sum boot://vi.pxe
cat boot://vi.pxe > system://replayed.pxe
sync system://
```

GDB used the matching kernel ELF and
`break checkpoint if pool->control.state == 1`. The first hit was the file's
creation transaction, sequence 20 with two images; it was allowed to checkpoint.
At the second hit, before checkpoint's home-write loop, QEMU was killed while
paused. The selected control was COMMITTED at sequence 22, with three images,
one descriptor block and payload CRC `3851112063`. The staged inode was number
12 with size 97,296 bytes; the staged home targets were bitmap block 1, indirect
block 4267 and inode block 4117. Ordered file data had reached the source before
the committed metadata, which remained in the journal. These are observed values
for this run, not fixed expectations for future qualification.

The stopped disk's pool was extracted with existing tools:

```sh
dd if=disk.raw of=committed-pool.raw bs=4096 skip=131328 count=392955 conv=sparse status=none
build/fs-tools/npfs-fuse -f committed-pool.raw /dev/shm/npfs-fuse-replay/mount
sha256sum /dev/shm/npfs-fuse-replay/mount/system/replayed.pxe
fusermount3 -u /dev/shm/npfs-fuse-replay/mount
```

FUSE reported its in-memory replay and exposed the complete 97,296-byte file in
mode 0444. Its SHA-256 matched the guest's source value:
`bdcc20ff1d2b1b8c9cb2a374bb8245e442c0b61a13f751afff942d940cdf8846`.
`cmp` against the immutable boot-source copy passed. A separate writable copy
was recovered using `fsck.npfs --replay`, which reported `structural check passed`.
Its extracted file matched the RAM view, and `diff -r` between both mounted
namespaces passed. The image-only inspector still refused the original source
with `journal replay required`.

After unmount, the complete committed pool's SHA-256 remained
`e00d59f23eab7172bd37523945aebe726772179f9d070040a0b4f95348a8aa96`.
The existing real committed-create image from task 1 also mounted in RAM with
`pending.txt` and matched a separate writable-fsck copy; its source digest
remained `953befbea53cf2118693848313c4f62e960897515296db614445b0d9905b5793`.

## Partition access and writable copied files

The same stopped disk was attached using `losetup --read-only --partscan`.
Following the updated [device recipe](../../../../fs/docs/npfs-host-tools.md),
its selected npfs partition's ACL was saved, read access was granted to UID 1000,
and FUSE mounted as that user. The mount was read-only and private to UID/GID
1000. It exposed the same recovered file and hash. `cp --no-preserve=mode`
produced a matching 97,296-byte file owned by 1000:1000 with mode 0644. After
unmount, the saved ACL was restored and matched byte-for-byte before loop detach.
The whole disk's SHA-256 remained
`407d03b6fbbd7ce532f5cf7c87333e91268114342bd98b80a0be67afe0870637`.
This qualifies Linux partition access and the non-root recipe, not physical USB.

The copy-mode follow-up was also checked with an ordinary temporary tree: at
umask 0022, plain recursive cp retained 0555/0444, while
`cp -r --no-preserve=mode` produced 0755/0644. Contents matched and creating and
deleting an entry in the copied directory succeeded.

## Matched lookup/copy measurements

The merged review found lookup reading each sibling inode before comparing its
name. Name comparison now precedes unrelated inode reads. Lookup still scans
directory records for duplicate matches; opendir still validates all listed
children and retains its sorted snapshots and continuation cookies.

Before implementation, a pool imported 4,175 existing regular files directly
under `/usr/share/man/man3`, totaling 6,406,415 bytes; 6,182 symlinks were excluded.
Source, 128 MiB pool (1 MiB EMPTY journal), copies and results were on tmpfs.
Both revisions used `-O2 -g3`, the same libfuse, mounting user and GNU cp 9.10.
Each sample used a new FUSE instance and fresh copy destination:

```sh
build/npfs-fuse pool.raw mount
/usr/bin/time -f 'wall=%e user=%U sys=%S maxrss_kib=%M exit=%x' \
  cp -r --no-preserve=mode mount/man3 copy-N
diff -r source-man3 copy-N
fusermount3 -u mount
sha256sum -c pool-before.sha256
```

| Revision | Wall time samples (s) | Median (s) | cp peak RSS samples (KiB) |
| --- | --- | ---: | --- |
| `77a8c33` | 21.13 / 20.02 / 19.77 | 20.02 | 2660 / 2656 / 2544 |
| `375b569` | 1.72 / 1.70 / 1.73 | 1.72 | 2832 / 2684 / 2544 |

Every copy comparison, source-file digest check and pool digest check passed.
The observed median difference is about 11.6 times on this workload. Timings cover
cp only, excluding daemon startup, formatting, diff and hashes; RSS excludes the
daemon. Caches were not dropped, and the Linux VM was not isolated from other
work. There was no profiling or physical-media measurement. The EMPTY workload
does not measure replay startup, overlay memory or worst-case lookup behavior.

## Scope and retained evidence

Independent code review found no correctness blocker; its minor feature-admission
wording finding was corrected. The shared loader validates the full staged
payload before publishing an overlay or writing homes. Writable fsck retains
exclusive/writable admission, sequence exhaustion and durable home/EMPTY ordering.
RAM replay preserves the source control/sequence and uses read-only admission.
Invalid payloads, allocation failures and extreme sizes were source-reviewed,
not manufactured or fault-injected. The mount checks traversed metadata; global
ownership checking remains fsck's job. The source must remain immutable throughout.

Inputs, copied files, digests, ACL snapshots, raw repeated timings and build logs
are retained outside Git at
`/home/chronium/src/pyxis-npfs-fuse-validation/task2/`. All own QEMU, GDB and FUSE
processes ended, and the read-only loop device was detached. A successful job in
the old builder could skip the optional FUSE target; the freshly pulled builder's
direct build above confirms its compilation. ThinkPad mount/copy is still pending.
