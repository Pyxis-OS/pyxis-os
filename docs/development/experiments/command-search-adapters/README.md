# Command-search adapter qualification

2026-10-19, command-search task 3. Nested Q35/KVM host CPU, four vCPUs,
2 GiB, OVMF, standard VGA, VirtIO block/network/rng and private virtio-fs.
Builder `pyxis-llvm23.1.3-3d69e83`, `make -j8 image BOOT_MENU_TIMEOUT=5`.
No compiler rebuild, upstream source, native/PXE action or performance claim.

## Revisions and baseline

Baseline main `01544db2`, userland `abd7aa8`, ports `e9906abb`: the retained
image at `23237441` has identical code, differing only in qualification docs.
Bare Lua, explicit missing-path refusal, package status and complete Remote
session drain passed. Source inspection found fixed lookup in `program_open`
and a separate plain-file sequence in Lua jobs.

The final implementation image built at parent `a9b43f8d`, userland `ad60db2`
and ports `45974b1`; fs remains main's `f3f22b6e`. Later ports `6b5e749` only
preserves the existing explicit hosted clangd default until packages task 4,
instead of moving that unrelated default prematurely. No adapter C change.
Private archive configuration selected disposable npfs media and explicit
fixture grants. Generated native children and full bundles contained no licensed
data. Shipped live/installed configuration was unchanged.

## Observed

- Startup supplied bin/package entries when installed bin fell back to the
  archive. `which` reported the same system and package targets as launch.
  A second disposable boot supplied distinct installed bin, boot/rescue and
  package entries in that order; an installed echo target won, a missing
  installed Lua resolved from the rescue entry, and the empty package view
  returned NOT_FOUND. The native caller reported zero problems with absent/empty lists despite
  held boot authority, explicit boot paths, ordered/reordered duplicates,
  appended project commands, system precedence, selected malformed/CUT refusal
  without fallback, and exact versus unsupported PATH projections.
- A child with an empty list and boot root could open the explicit boot image
  but could not find its bare name; launcher lookup was absent. It exited zero.
  Finding code supplied no descendant-launch authority.
- Normal explicit recovery rollback and HTTP install produced fresh sets 6/7.
  A running caller retained version-2 bundle resources while a new lookup saw
  version 1 after a normal native current-pointer replacement; restoring current
  returned later lookups to version 2. A first fixture incorrectly named a set
  already collected at boot; the valid run used the retained fresh sets.
- Lua ordinary launch, captured plain and complete-bundle jobs, and a pipeline
  with a bundle stage passed. The bundle child read its declared resource root;
  withholding its required memory role refused admission. Captures drained,
  native results completed and close returned OK. Passing the same `$commands`
  publication through an ordinary shell command succeeded.
- Neovim executable probing, libuv rejection of unsupported PATH, and successful
  exact default-label PATH passed. luv copied resource views from the admitted
  process selection, kept them readable after process close and refused boundary
  escape. The actual LSP transport hook opened a fixture server's declared root.
  Two active versions made a new ambiguous URI read refuse; after one server
  exited, new reads used the remaining version while an existing buffer retained
  its original version. Servers ended through stdin EOF; Unix signal handling
  remains unsupported and was not substituted or changed.
- Read-only GDB at native command-list INFO showed READ rights 1, count 2,
  labels `bin://`/`packages` and directory masks 7/7. The ordinary syscall stack
  was inspected without debugger calls or memory changes. After orderly shutdown,
  the publication/check caller's scratch pool passed `fsck.npfs`.

## Review correction

Ports `422883c` fixes explicit libuv command-list replacement before inherited
CAP validation and inherited TEXT staging. Userland was rebased to `aca3e42`
on main `bc8de46`; the integrated image was rebuilt on parent main `63d84152`.
A focused native caller in the same four-CPU QEMU configuration launched two
children successfully over exported TEXT and wrong-protocol CAP parent entries.
Both children exited zero. Without an override both malformed parents refused;
a wrong-protocol override and explicitly supplied TEXT `commands` also refused (the environment entry at that revision; now named `path`).
The caller reported zero problems and the Remote session drained completely.
Evidence: `override/check.jsonl`, `override/check.txt` and
`override-image-build.log` below the shared qualification directory.

## Limits

These are private fixture checks, not a new clangd native qualification or native
compiler/build-chain delivery. The existing explicit hosted clangd default stays
until packages task 4; callers may configure a bare command using the list.
Libc process replacement/execvp remains unavailable, and native descendant build
chains retain their separate nested-execution gate. No concurrent publisher
stress, allocation exhaustion, fault injection or power-cut coverage is claimed.
Raw logs, fixture sources, configuration and debugger transcript remain under
`/shared/present/command-search/task3/`.
