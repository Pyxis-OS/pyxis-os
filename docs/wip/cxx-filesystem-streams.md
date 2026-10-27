# C++ filesystem and streams

Assigned to Daybreak (chronium, 2026-10-26). The accepted proposal and initial
source analysis are in `/shared/present/cxx-filesystem-streams/PROPOSAL.md`.
The milestone adds a native libc++ filesystem backend and qualifies the existing
stream profile, then removes port-local C-stdio replacements made redundant by
the standard library. It does not grant new roots to any program.

## Accepted contract

- `scheme://` is one implementation-defined root component. `path` preserves
  input bytes and lexical operations can describe a leading `/`, but native
  filesystem I/O rejects that spelling. Lookup never URL-decodes, case-folds
  or reconstructs path components.
- Native filesystem operations use caller-held roots and cwd. Metadata and
  iteration never open a provider or make an HTTP request. Denied, missing,
  unsupported and concurrent-change outcomes remain distinct. A recursive
  mutation can leave confirmed partial effects and does not imply sync.
- Permissions, unavailable timestamps and free space are not guessed. Symlink
  and metadata setters report unsupported. `copy_file` remains unsupported
  until its content and attribute contract can be preserved by native APIs.
- The current C locale, iostream, wide-character, random-device and thread
  profile stays enabled. `<fstream>` uses the existing FILE/descriptor rights,
  staging and close-error contracts.

## Tasks

- [ ] Merge the native libc helper PR and libc++ fork PR. The fork source head
  must be recorded in a rebuilt compiler container before Pyxis switches on
  `LIBCXX_ENABLE_FILESYSTEM`.
- [ ] Merge the Pyxis SDK pin/profile PR after exact-head CI and a 4-CPU QEMU
  check. Qualify narrowed roots, provider refusal, path parsing, native
  mutations and iostream/fstream close and flush behavior.
- [ ] Remove only the libultraship and SoH C-stdio replacement patch hunks
  made redundant by native `<filesystem>` and streams. Preserve each port's
  Pyxis authority, publication and error handling. Report removed patch lines
  and run the affected ports in QEMU.

## Current qualification and limits

The published compiler image records fork commit `8a4d001d5480`. The parent
draft builds a complete full-distribution ISO with it, and the SDK's libc++
cache records `LIBCXX_ENABLE_FILESYSTEM=ON`. In one 4-CPU Q35/KVM boot with
4 GiB RAM, VirtIO net and a disposable writable virtio-fs export, an external
program built by that compiler against the SDK passed 27 checks: native
status and iteration, recursive create/remove counts, rename, resize,
read-only refusal, `canonical`, `absolute`, and iostream/ifstream/ofstream/
fstream read, seek, write, flush and close. `copy_file` returned the intended
unsupported error. The Remote profile did not have an HTTP root, so its
provider status probe proves only missing-root refusal; source inspection
confirms the native-only route never opens a provider.

A host Pyxis-parser build passed the accepted path cases and 200,000
deterministic forward/reverse iterator paths. These are QEMU and host checks,
not a native-hardware result. Exact-head CI is still required before merge.
