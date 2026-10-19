# Package-command view qualification

2026-10-19, command-search task 2. Nested Q35/KVM host CPU, four vCPUs,
2 GiB, OVMF, standard VGA, VirtIO block/network/rng and private virtio-fs.
Existing compiler container `pyxis-llvm23.1.3-3d69e83`, `make -j8 image
BOOT_MENU_TIMEOUT=5`. No compiler rebuild, upstream download, native/PXE action,
power-cut run or merged test framework.

## Baseline and build

Unchanged main `e980a53b9832`, userland `4f8aece9`, ports `e9906abb`,
fs `d2fb97c5`: successful official main workflow global 3350/UI 3245,
image artifact 14018, embedded kernel revision checked. Remote echo/Lua and
session completion passed; `pkg status` refused the shipped live image's absent
npfs inspection authority. No package command projection/view existed. This was
a functional/source baseline, with no performance claim.

The final full default image at parent `23237441b845` and userland
`abd7aa815e1a` built successfully, including SDK, default ports and applications.
It rebases onto main `cd9a21eb` and inherits that main's fs pin `f3f22b6e`;
ports remain `e9906abb`. The final interactive repeat used that image with a
private archive configuration binding a 160 MiB scratch GPT disk containing a
128 MiB npfs pool. System/packages roots were explicitly provisioned through
normal boot configuration and fixture bundle grants; shipped configuration was
unchanged. The host packer/checker produced and verified two command-bearing
versions plus a colliding package, using only generated private code/resources.
The bundle's explicit export and declared command are both `alias`. Initial
selected sets lacked projections; a loopback HTTP repository supplied `pkg`.

## Observed

- `pkg verify` refused the missing index. Explicit `pkg reindex` verified files
  and created only the active set's projection; a second invocation refused its
  existing index. Cancel left the active selection unchanged. Install over HTTP
  published and verified set 2; duplicate exports refused before activation,
  leaving set 2 current. Rollback, remove-to-empty and rollback-from-empty
  published fresh sets 3, 4 and 5, each passing `pkg verify`.
- A running private SDK client captured set 2, then switched `current` to set 1
  with the normal native replace/sync operations. Held code/resources still
  identified version 2; a fresh resolution identified version 1. Reuse through
  a second live view retained set 2. Captured empty set reuse stayed empty after
  restoring current. An independent volume refused cross-volume reuse.
- Plain and complete bundle selections launched real children; all three exited
  zero. Bundle resource contents and the 2 MiB manifest stack were retained.
  With current switched, required package grants from matching independent
  authority still opened the selected set's version (1 or 2), while authority
  from another volume refused.
- Directories exposed only indexed names: `store` was absent, mutations refused,
  subdirectory construction refused and write-bearing target requests refused.
  An attenuated view allowed its READ FILE but refused a bundle needing wider
  directory rights. CUT of the live view denied its derived capture.
- Missing and short/damaged active indexes refused IO. A capture taken before
  the damage remained usable; the private fixture restored the index afterward.
  Empty list resolution returned NOT_FOUND without inventing default entries.
  The final SDK caller reported zero problems with complete Remote drain.
  After orderly shutdown the scratch pool passed `fsck.npfs`.
- Binding a retargetable independently authorized source root, then retargeting
  its slot to another volume, left the bound record/tree on version 2. Binding
  the now-unrelated source refused. A command view could not bind itself to
  expose its root; a live capture refused. CUT of either the independently held
  source or fixed capture denied access through the bound root.
- During the earlier implementation qualification, read-only GDB stopped at `npfs_commands_perform` for COMMAND_CAPTURE, with
  rights 7, directory kind 2 and IF set (`eflags=0x282`); the stack was the sole
  filesystem worker. No debugger function calls or state changes were used.

Host ordinary pack/check cases also covered absent exports, plain targets and declared bundle commands,
bundle-inner traversal refusal and undeclared bundle aliases. Reusing the host
bundle validator exposed its stale package-grant schema; it now matches native
package selection and resource-name restrictions. Source review corrected a
long-name generated-suffix miss and a source-retarget lifetime race. The final
repeat above includes the native pinned-root correction; the remaining default/conventional adapter
qualification belongs to task 3.

## Limits

This qualifies explicit held-list package selection, not automatic search from
shell/editor/libc adapters or native build chains. Those migrations remain task
3 and the separate nested-execution gate. No concurrent publisher stress,
allocation-exhaustion, transport-fault or power-cut coverage is claimed. Boot-only
collection scope is unchanged. Raw logs, private configuration/caller sources,
repository and GDB transcript stay in `/shared/present/command-search/task2/`.
