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

The unpublished parent branch built a complete image with the userland helper
revision. A target libc++ archive with filesystem enabled built and linked;
on a 4-CPU Q35/KVM guest, an external probe passed native status, iteration,
recursive create/remove counts, rename, resize, read-only refusal, `canonical`,
`absolute`, and iostream/fstream I/O. A host Pyxis-parser build passed the
accepted path cases and 200,000 deterministic forward/reverse iterator paths.
These are QEMU and host checks, not a native-hardware result. The parent SDK
pin and exact-head CI remain pending the compiler-container rebuild.
