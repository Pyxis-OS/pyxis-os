# Native npfs hard links

Owner decisions accepted, 2026-10-19. Task 1 is merged and task 2 is assigned.
Accepted tasks continue automatically after their predecessor merges; stop for
new behavior, authority or lifetime decisions. Implementation builds on merged [explicit durability](explicit-durability.md)
task 2b ([#975](https://git.internal/PyxisOS/pyxis-os/pulls/975)).

The owner already accepted [package decision 2](packages.md#decisions): regular
files only, a link count in the inode reserve behind a `read_only_compatible`
feature bit, fsck accepting shared inode references, and reclamation after the
last name and held reference disappear. Symbolic links are a later milestone.
The deliverable is native `ln`, shared contents/identity through either name,
and atomic stable-name replacement using link then rename and explicit sync.
Package data/program slices do not depend on this; later stable library names do.

## Existing boundaries

At fs pin `25337999`, the [format](../../fs/docs/npfs-format.md) has a 64-byte
inode reserve at offset 192; older decoders ignore it. Regular-file parent is
already zero, while directories retain unique ancestry. At that pin the host checker rejects
multiple names, but already walks block ownership once per inode.
At that pin, headers are immutable while mounted and valid disagreeing copies
refuse opening. Task 1 replaces that rule with the accepted feature-gain rule
below; task 2 adds the first-use transition.

The fs repository owns codecs, formatter, checker, inspector and replay/FUSE
support. The current running-transaction writer is
`kernel/fs/npfs_store.c` in Pyxis; native protocol/adapter changes belong there.
Use those existing implementations, with no second writer. Publish fs changes
before their parent pin; libc and commands remain in userland.

## Accepted contract

- Names in one volume may identify one regular inode. Each alias observes the
  same cached data, size and object identity; no copied file or shared extents
  between different inodes. Directories keep their tree and parent rules.
- Add-name/count increment and remove-name/count decrement are whole operations
  in the running transaction, including undo and preflight reservations. Count
  edits preserve the durable mapping/size prefix and pending dirty data, without
  publishing unwritten growth. A move preserves the source count; replacing
  rename decrements its victim. Only the last unlink marks DETACHED;
  reclamation also waits for held references. SHRINK
  still affects the shared inode and must coexist correctly with DETACHED.
- Keep data-before-mapping ordering, staged-image reuse exclusion, bounded cleanup
  and the existing BSP worker/VM ownership contracts. LINK does not imply a
  data/namespace sync; first-use feature activation has the header flushes below.
  File/directory/mount sync retain their explicit durability rules.
- New feature knowledge must not admit an unaudited writer. Each mutating consumer
  explicitly admits this feature only after its count updates are implemented;
  unknown read-only-compatible bits continue to prohibit writes and replay.

## Accepted owner decisions (2026-10-19)

1. **Native interface and authority. Accepted:** source-parent DIRECTORY operation
   with two single-component names and a destination-directory grant, matching
   rename's delivery/lifetime handling. Require source LOOKUP, READ_FILES and
   WRITE_FILES, plus destination CREATE; reject existing destination names.
   Same-volume regular files only; no linking an orphan through a held FILE.
   Names carry no persistent rights ceiling: source read-only or write-only
   authority must not gain the missing content right through another directory.
   Lookup of the new name uses its destination grants; later grant closure/cut
   does not roll back completed namespace edits, as with rename. Rename between
   two names of the same inode succeeds without removing either name. Other
   backends refuse LINK honestly.
2. **Count and first-use activation. Accepted with the owner's change:** an
   explicit little-endian 32-bit count in the inode reserve, maximum `UINT32_MAX`;
   overflow refuses before effects. FREE/DETACHED have zero; directories use no
   count field. Without the feature bit, retain legacy implicit one/zero rules
   and ignore the reserve. Gaining the bit must preserve the interpretation of
   untouched legacy inodes. The bit remains set after aliases disappear.

   Two independently valid header copies may differ only when all three feature
   masks in one are supersets of the other's, with at least one strictly larger.
   All other encoded bytes must agree, apart from each copy's checksum. Select
   the superset, then apply feature admission to it; a writable open repairs the
   lagging copy. Read-only opening does not repair. All other differences still
   refuse opening; unknown bits never fall back to the less restrictive copy.

   Existing pools gain the bit on their first eligible LINK, after request
   validation and before staging the link: write one header with the bit, flush,
   write the other, flush, then proceed. A crash between writes resolves to
   feature-present. Do not activate at mount: link-free pools must remain
   writable by older kernels. Headers are immutable except this feature-gaining
   transition; formatting may set the bit but is not a prerequisite. Failed or
   uncertain activation does not stage LINK and follows existing I/O-failure rules.

   **Known edge:** an older kernel booted after a crash inside the two-flush
   window refuses to open the pool, because it rejects the differing headers.
3. **Checking and recovery. Accepted:** fsck counts all incoming file names and
   requires agreement with the effective count. Walk each inode's mapping once;
   keep rejecting cross-inode block sharing, directory aliases/cycles, linked
   DETACHED inodes and broken cleanup lists. A mismatch fails checking, with no
   guessed repair. Replay remains validated whole-transaction replay; host fsck
   checks global structure afterward. Kernel mount retains local validation,
   without adding a whole-pool fsck. Older file readers remain usable; older fsck
   may reject aliases, and older writers/replayers refuse the feature. No automatic
   count-repair mode is included.

## Implemented foundation

[fs #43](https://git.internal/PyxisOS/pyxis-fs/pulls/43) provides the codecs and
host tools. Attached legacy files with stored count zero decode as effective
one after feature gain; feature-aware encoders emit explicit positive counts.
This preserves untouched old inodes and redo images without a pool-wide rewrite.
Conservative default write admission still supports no RO-compatible features;
only the audited host replay/repair path opts into the hard-links bit. Native
header selection/repair and count-maintaining mutations remain task 2.

The [task-1 record](../development/experiments/npfs-hard-links-task1/README.md)
separates baseline/host fixture observations and integration checks from the
unimplemented native operations and remaining durability qualification.

## Ordered tasks

Capture the namespace/writeback baseline before task 2 changes the native writer;
task 4 repeats matched workloads. Task 1 records host correctness observations;
no native performance measurements are claimed.

- [x] **1. Format and host-tool foundation (fs), assigned 2026-10-19.** Define the
  feature/count codec,
  optional formatter support and inspector output; make checker/replay/FUSE
  interpretation count-aware. Update the fs format contract and shared header
  selection for strict feature-mask supersets, including writable lagging-copy
  repair and legacy inode interpretation after activation. Keep write admission
  separate from decoding so pinning this foundation cannot enable the old kernel writer on linked pools.
  Qualify legacy/feature-enabled fixtures, shared names/count disagreement,
  either header as the superset, writable repair, read-only non-repair and
  rejection of every other header difference through existing host tools.
  **After this task the owner can:** inspect and check a counted-link fixture
  with the new tools. This is the small first task; native creation follows.
- [ ] **2. Native transaction and lifetime integration (Pyxis, fs pin), assigned 2026-10-19.** Add LINK
  and the approved authority checks; integrate counts with create, unlink,
  replacement/move, shared cached inodes, shrink and last-handle cleanup. Admit
  feature-enabled writes only with the complete writer integration. Before the
  first LINK on an existing pool, perform the ordered two-header/two-flush
  activation; do not activate at mount. Update interfaces beside code and qualify
  first-use activation, interruption between header writes and normal operations
  in QEMU/GDB.
  **After this task the owner can:** inspect two native names retaining one
  inode through unlink and replacement in QEMU.
- [ ] **3. SDK/libc and command (userland, then parent pin).** Expose the native
  component/path operation, libc `link`, and `ln` without symbolic-link flags.
  Report a real link count through native INFO and libc metadata where known;
  unsupported providers must not invent one. Qualify denied authority, existing
  destination, directory/cross-volume sources, and shared-write identity.
  **After this task the owner can:** create aliases with `ln`, edit either name,
  and publish a stable name with link then replacing rename and explicit sync.
- [ ] **4. Durability and owner qualification.** Repeat matched namespace
  workloads against the baseline. Qualify ordinary builds, QEMU/GDB, checkpoint and committed replay, last unlink
  with a held file, unlink/rename between aliases, and count-limit refusal.
  Arrange an owner-approved disposable legacy pool for a native first-use check,
  with no formatting requirement. Record the older-kernel activation-window
  refusal and remaining gaps honestly.
  **After this task the owner can:** retain an old open file while atomically
  replacing its stable name, then sync/reboot and verify names, counts and bytes.

The owner assigned task 2 and accepted automatic progression after each merge.
New decisions still stop for review. Package integration, dynamic linking,
symbolic links and a filesystem repair utility are outside this milestone.
