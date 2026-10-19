# Native npfs hard links

Owner-assigned proposal, 2026-10-19. No implementation is authorized by this
document. Implementation follows [explicit durability](explicit-durability.md)
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
already zero, while directories retain unique ancestry. The host checker rejects
multiple names today, but already walks block ownership once per inode.
Pool headers are immutable while mounted; two valid disagreeing headers refuse
opening. A first live link cannot safely enable a feature by rewriting a header.

The fs repository owns codecs, formatter, checker, inspector and replay/FUSE
support. The current running-transaction writer is
`kernel/fs/npfs_store.c` in Pyxis; native protocol/adapter changes belong there.
Use those existing implementations, with no second writer. Publish fs changes
before their parent pin; libc and commands remain in userland.

## Proposed contract

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
  and the existing BSP worker/VM ownership contracts. Linking does not silently
  synchronize. File/directory/mount sync retain their explicit durability rules.
- New feature knowledge must not admit an unaudited writer. Each mutating consumer
  explicitly admits this feature only after its count updates are implemented;
  unknown read-only-compatible bits continue to prohibit writes and replay.

## Decisions proposed to the owner

1. **Native interface and authority. Default:** source-parent DIRECTORY operation
   with two single-component names and a destination-directory grant, matching
   rename's delivery/lifetime handling. Require source LOOKUP, READ_FILES and
   WRITE_FILES, plus destination CREATE; reject existing destination names.
   Same-volume regular files only; no linking an orphan through a held FILE.
   Names carry no persistent rights ceiling: source read-only or write-only
   authority must not gain the missing content right through another directory.
   Lookup of the new name uses its destination grants; later grant closure/cut
   does not roll back completed namespace edits, as with rename. Rename between
   two names of the same inode succeeds without removing either name. Other
   backends refuse LINK honestly. Alternative: introduce a separate alias-publication right;
   defer that extra grant profile until a consumer needs it.
2. **Count and feature activation. Default:** an explicit little-endian 32-bit
   count in the reserve, maximum `UINT32_MAX`; overflow refuses before effects.
   Feature-enabled linked files have count at least one; FREE/DETACHED have zero,
   directories use no count field. Without the bit, retain the legacy implicit
   one/zero rules and ignore the reserve. Opt new pools in explicitly at format
   time; the bit remains set even after aliases disappear. Existing pools keep
   their ordinary operations but refuse LINK until a later offline conversion.
   No mounted header rewrite or format-version bump. Alternatives: 64-bit counts
   or include an offline converter now; the latter needs separate two-header
   interruption qualification.
3. **Checking and recovery. Default:** fsck counts all incoming file names and
   requires agreement with the stored count. Walk each inode's mapping once;
   keep rejecting cross-inode block sharing, directory aliases/cycles, linked
   DETACHED inodes and broken cleanup lists. A mismatch fails checking, with no
   guessed repair. Replay remains validated whole-transaction replay; host fsck
   checks global structure afterward. Kernel mount retains local validation,
   without adding a whole-pool fsck. Older file readers remain usable; older fsck may
   reject aliases, and older writers/replayers refuse the feature. Alternative:
   a count-repair mode, deferred until an explicit repair contract exists.

## Ordered tasks

Capture the namespace/writeback baseline before task 2 changes the native writer;
task 4 repeats matched workloads. No measurements are claimed by this proposal.

- [ ] **1. Format and host-tool foundation (fs).** Define the feature/count codec,
  explicit formatter opt-in and inspector output; make checker/replay/FUSE
  interpretation count-aware. Keep write admission separate from decoding so
  pinning this foundation cannot enable the old kernel writer on linked pools.
  Qualify legacy and feature-enabled disposable fixtures, including shared names
  and count disagreement, through existing host tools.
  **After this task the owner can:** inspect and check a counted-link fixture
  with the new tools. This is the small first task; native creation follows.
- [ ] **2. Native transaction and lifetime integration (Pyxis, fs pin).** Add LINK
  and the approved authority checks; integrate counts with create, unlink,
  replacement/move, shared cached inodes, shrink and last-handle cleanup. Admit
  feature-enabled writes only with the complete writer integration. Update
  interfaces beside code and qualify ordinary operations in QEMU/GDB.
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
  Arrange an owner-approved disposable feature-enabled pool for a native check;
  no conversion of the installed pool. Record remaining gaps honestly.
  **After this task the owner can:** retain an old open file while atomically
  replacing its stable name, then sync/reboot and verify names, counts and bytes.

Each implementation task needs owner go. This plan does not authorize package
integration, dynamic linking, symbolic links or a filesystem repair utility.
