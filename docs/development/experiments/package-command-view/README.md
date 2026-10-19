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

The task's full default image built successfully twice, including the exported
SDK, all default ports and applications. The qualified working-tree code is now
committed as kernel `09b68f70` and userland
`14b1c4610805575356b8cb462c926d633a476902`; its embedded kernel build revision
was `a5b7e166c166` before those implementation commits. Later commit/pin and
record updates change provenance, not the qualified implementation. A private archive configuration bound
a 160 MiB scratch GPT disk containing a 128 MiB npfs pool; system/packages roots
were explicitly provisioned through normal boot configuration and fixture bundle
grants. Shipped configuration was unchanged. The host packer/checker produced
and verified two command-bearing toolchain versions plus a colliding package,
using only private generated code/resources. Old selected sets initially lacked
command projections. A local HTTP repository supplied the normal native client.

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
- Read-only GDB stopped at `npfs_commands_perform` for COMMAND_CAPTURE, with
  rights 7, directory kind 2 and IF set (`eflags=0x282`); the stack was the sole
  filesystem worker. No debugger function calls or state changes were used.

Host ordinary pack/check cases also covered absent exports, plain/bundle aliases,
bundle-inner traversal refusal and undeclared bundle aliases. Reusing the host
bundle validator exposed its stale package-grant schema; it now matches native
package selection and resource-name restrictions. Source review corrected a
long-name generated-suffix miss; the remaining default/conventional adapter
qualification belongs to task 3.

## Limits

This qualifies explicit held-list package selection, not automatic search from
shell/editor/libc adapters or native build chains. Those migrations remain task
3 and the separate nested-execution gate. No concurrent publisher stress,
allocation-exhaustion, transport-fault or power-cut coverage is claimed. Boot-only
collection scope is unchanged. Raw logs, private configuration/caller sources,
repository and GDB transcript stay in `/shared/present/command-search/task2/`.
