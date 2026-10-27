# C++ filesystem and streams

The Pyxis libc++ fork provides a native `<filesystem>` backend. Its SDK build
uses the exact fork revision recorded by the installed compiler and enables
`LIBCXX_ENABLE_FILESYSTEM`. `<iostream>` and `<fstream>` use the existing C
locale, FILE and descriptor implementation; neither facility creates a grant.
See the [SDK C++ profile](sdk.md#c) for the complete supported API list.

## Paths and authority

`scheme://` is one root component. `std::filesystem::path` preserves the input
bytes; lexical operations can describe a leading `/`, while filesystem I/O
refuses that spelling. Lookup does not URL-decode, case-fold or reconstruct
components. Metadata and directory operations use the caller's held roots or
cwd and do not invoke a provider. A denied root, missing file, unsupported
operation and concurrent directory change remain distinct errors.

The supported operations include status, iteration, create/remove, rename,
resize, counted recursive operations, hard links where the filesystem supports
them, `canonical`, and available metadata reads. Recursive mutations can leave
confirmed partial effects and do not imply a sync. `copy_file` refuses until
its content and metadata contract can be preserved. Symlinks, permission and
timestamp setters, free-space reporting, provider-backed filesystem operations
and a time-zone database remain unavailable; these calls report errors rather
than guessed values.

Streams use the FILE/descriptor rights of the underlying handle. For staged
output, callers still have to observe flush, close and explicit commit results
under the [stdio contract](../userland/stdio.md). An `ofstream` destructor is
not a durability or publication confirmation.

## Port integration

LibUltraShip uses native filesystem metadata and an `ifstream` to reload
configuration, and the selected SoH port uses upstream filesystem/stream code
for ordinary preset, randomizer, time-split and archive paths. Its state stays
under `home://soh`, while packaged archives stay under `app://`. Neither port
receives additional roots. LibUltraShip and SoH retain their explicit temporary
file sync, replacement, parent sync and uncertain-result paths for configuration
and save publication; a plain stream close cannot replace those barriers.
LibUltraShip still excludes folder archives and hosted path discovery from its
selected profile.

## Qualification

A clean full image built with the published compiler image recording fork
`8a4d001d5480`. In one 4-CPU Q35/KVM guest, an external C++ program passed
27 native filesystem and stream checks, including read-only refusal, directory
iteration, mutation, seek, flush and close. A host parser probe checked the
accepted lexical examples and 200,000 deterministic forward/reverse iterator
paths. These checks are QEMU and host evidence, not native-hardware evidence.
The separate port build and SoH guest smoke are recorded in the port cleanup
PR; they do not qualify interrupted save publication or audio hardware.
