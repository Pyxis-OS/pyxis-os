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
OBSERVE | RETIRE rights (`0xa`). A conflicting RO/RW configuration for two names
of one persistent selector rejects the plan before FAT admission and starts rescue;
the selected media remains unchanged.

Earlier VirtIO/USB saves used userland `a4859dd`; subsequent changes preserve
zero-right native helper semantics and validate retained controllers on failed
publication/release paths. The final-head RO/refusal run uses `f5205a8` and the
same kernel/port code. The retained-controller correction was independently
reviewed from source; no allocation-failure injection was added.

## Cost and backing

After the concurrent hosted-Clang target build ended, run the preserved control
and final integration ISO in order A1/B1/A2/B2/A3/B3, with the baseline command,
media, geometry and four-CPU configuration. No local compilation runs; guest host
CPU snapshots show idle capacity. Earlier saturated-build attempts are discarded.
Each boot has one warm-up and five verified samples, with zero failures. Values
below are elapsed milliseconds: median [minimum–maximum]. Clock-loop cost is
microseconds/read over 1000 reads; it is not subtracted. GDB is detached during
measurement and FILE profiling is off. The iobench executable is byte-identical
in both images (SHA-256 `da0514a1fe029aa51b72d26a3cb2fbcc2cd817c0504e923f96bd432737cc092b`).

| Boot | Clock µs | Open ms | Payload ms | Complete ms |
| --- | ---: | --- | --- | --- |
| A1 control | 37.191 | 0.466 [0.433–0.573] | 4.272 [3.950–4.928] | 5.591 [5.433–6.435] |
| B1 integration | 161.854 | 1.692 [0.671–2.079] | 17.540 [15.859–18.981] | 23.094 [20.692–24.051] |
| A2 control | 38.136 | 0.521 [0.427–0.678] | 5.002 [4.190–7.644] | 6.584 [5.737–9.760] |
| B2 integration | 37.068 | 0.432 [0.375–1.399] | 3.982 [3.871–4.119] | 5.875 [5.235–7.489] |
| A3 control | 37.000 | 0.509 [0.444–0.865] | 4.190 [3.973–5.117] | 5.536 [5.449–7.208] |
| B3 integration | 139.581 | 0.730 [0.594–2.728] | 7.660 [6.731–16.312] | 11.068 [9.002–22.814] |

The four-CPU comparison is **inconclusive**: two integration boots have much
higher clock-call and I/O costs, while B2 lies in the normal control range. The
source diff changes neither clock/syscall scheduling nor the npfs read path; that
inspection does not explain the slow boots or establish performance neutrality.
No outlier is silently omitted from this table.

A matched one-CPU follow-up changes only QEMU's `-smp 1`; same command, five
verified samples each and zero failures:

| Boot | Clock µs | Open ms | Payload ms | Complete ms |
| --- | ---: | --- | --- | --- |
| Control | 37.611 | 0.472 [0.466–0.475] | 4.246 [4.207–4.942] | 5.759 [5.637–6.371] |
| Integration | 38.519 | 0.457 [0.369–0.482] | 4.607 [4.273–5.243] | 6.531 [5.741–6.810] |

Payload/complete medians are about 9%/13% higher in this single pair, with
overlapping ranges. This is an observed cost, not an attributed regression or a
claim that the four-CPU effect is resolved. Stable-host/native repetition is
needed before judging latency; these tiny synchronous reads include nested-VM
clock and scheduling overhead. The npfs partition remains byte-identical after
all runs. No optimization beyond task 5 is added.

Accounted PMM snapshots with the ordinary npfs shell are 4616 control versus
4619 integration allocated frames, both returning to 4251 after shell exit.
The linked shell grows from 59 to 62 eager image pages (12 KiB); this explains
the live allocated-frame difference. Boot archive bytes grow from 83131904 to
83338752 (206848 bytes); the integration boot has 53 fewer allocator-managed
frames (212 KiB of additional boot-reserved backing, including kernel/alignment).
This separate boot-image cost is not hidden by the equal post-exit allocation count. FAT instance/workspace counts stay zero. These are system
allocated-frame snapshots, not private RSS or a transient peak.

QEMU/GDB jobs are stopped after each check. Raw logs/images/screenshots stay local;
no benchmark infrastructure, fault hook or new klog line is introduced. Physical
USB, native ESP, unplug, torn-sector and power-loss behaviour are unqualified.
The [owner-present ESP run sheet](../../wip/fat32-volumes.md#owner-present-native-esp-run-sheet)
is RO-only; it requires approval and presence and authorizes no real ESP write.
