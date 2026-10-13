# FAT32 trusted workflow qualification

Task 5 uses accepted mount-only selection, shell-owned bindings and declared
selected editor grants. Control: [exact-main baseline](fat32-workflow-baseline.md),
Pyxis `d3bc9b5aaa14`; fs `414be0ac0b06`, userland `c472de7d5d9a`, ports
`f93becb9e545`, lwIP `a1aadb91a503`. Integration code: Pyxis `fd59b7458912`,
userland `f5205a8`, ports `2ff5051`; fs/lwIP unchanged. Later parent commits
record these results and pin the published dependencies. Default image builds
with `make -j8 image BOOT_MENU_TIMEOUT=5`, using the pinned
`pyxis-llvm23.1.3-4c23a20` builder; no compiler rebuild or external source change.

## Executed scratch workflow

Interactive QEMU 10.2.2, q35/KVM, host CPU, four CPUs, 1 GiB RAM, OVMF. VirtIO
uses modern block transport and a disposable GPT image with disjoint 64 MiB FAT
and npfs partitions. Configuration selects fixed RW FAT admission, ordinary RO
FAT roots, explicit selected DIRECTORY/MOUNT sources and opt-in mount-only
inventory; no `boot.install` or raw DISK grant. Identifiers/configuration stay local.

- Ordinary FAT redirection refuses writes. The common Neovim manifest receives
  the selected directory as `edit://`; editing/saving there succeeds. `cat` reads
  the saved content through the ordinary RO root, and explicit sync succeeds.
- With another configured space holding a view, release moves cwd to home,
  removes the caller's binding and reports BUSY. The second owner releases its
  references; retry succeeds. FAT instance/workspace/retired-node counts return
  to zero. No handle is revoked.
- A manual RW mount works beside a live npfs partition without a raw whole-disk
  claim. A trusted `session boot://shell.pxe` successor receives its selected
  sources and releases from the mounted cwd, including original startup cwd
  handles. Final release drains all FAT state.
- Independent mtools reads verify `TASK5.TXT` and untouched `HELLO.TXT`;
  `fsck.fat -n` reports a clean FAT volume.

USB qualification attaches a disposable single-LUN USB BOT stick through QEMU
xHCI beside a separate npfs VirtIO disk. Its configured local session/mux forwards
selected sources; Neovim in a pane saves `USB.TXT`, then holds that file open via
`io.open`. GDB observes two retained FAT data nodes, owned dirty state and no
mutation failure. Clean power from another space exits QEMU successfully while
the editor/file are open. Independent mtools reads the saved and untouched files;
`fsck.fat -n` is clean and the strict inspector reports clean/no-hard-error state.
This qualifies sealing without retirement or a new session-termination phase.

A separate disposable GPT disk boots directly through its ESP (no optical origin
or debugger-fabricated provenance), attached RO. The normal `boot-esp` binding
works: list/read/RO-control observation; sync lacks WRITE and refuses; wrong-domain
release refuses before unbinding; redirection refuses. Valid release moves cwd
home and drains all FAT state. Full disk hashes before/after agree.

Final-head refusal checks additionally launch a valid bundle with a required
unavailable selected directory: launch is denied before publication. RW admission
against the existing RO mount returns BUSY; after retirement, RW admission of RO
media refuses with READ_ONLY. Fresh RO admission and release succeed, all FAT
state drains and the disk hash is unchanged. The utility reports actual RO
OBSERVE | RETIRE rights (`0xa`).

Earlier VirtIO/USB saves used userland `a4859dd`; subsequent changes preserve
zero-right native helper semantics and validate retained controllers on failed
publication/release paths. The final-head RO/refusal run uses `f5205a8` and the
same kernel/port code. The retained-controller correction was independently
reviewed from source; no allocation-failure injection was added.

## Cost and backing

Matched timing after-runs are pending a quiet VM: a concurrent hosted-Clang target
build saturated its CPUs. Loaded attempts are discarded rather than compared with
the quiet control. This is a measurement limitation, not a measured regression.
The iobench executable is byte-identical in control/integration; profiling is off,
GDB detached for timings, and clock-call overhead is not subtracted.

Accounted PMM snapshots with the ordinary npfs shell are 4616 control versus
4619 integration allocated frames, both returning to 4251 after shell exit.
The linked shell grows from 59 to 62 eager image pages (12 KiB); this explains
the live difference. FAT instance/workspace counts stay zero. These are system
allocated-frame snapshots, not private RSS or a transient peak.

QEMU/GDB jobs are stopped after each check. Raw logs/images/screenshots stay local;
no benchmark infrastructure, fault hook or new klog line is introduced. Physical
USB, native ESP, unplug, torn-sector and power-loss behaviour are unqualified.
The [owner-present ESP run sheet](../../wip/fat32-volumes.md#owner-present-native-esp-run-sheet)
is RO-only; it requires approval and presence and authorizes no real ESP write.
