# Boot archive assembly

The kernel maps and validates the uncompressed `newc` archive supplied as the
sole Limine module. The build separates compilation, install trees, guest-tree
assembly and archive packing.

Userland's `make install` owns its selected programs, `init` and assets, exporting
`build/userspace-root` through the root build. Objects and debug ELFs stay in
`build/userspace`. Ports owns `ports/install.lua`, which selects its executables,
licenses and support files into `build/ports-root`. Host port tools stay out
of the guest tree.

`boot/initrd.lua` selects from those trees by `DISTRIBUTION`. The default,
`core`, is the core archive ([decisions 18 and 64](../wip/pkg-upgrade.md)): the
programs `boot/rescue.list` names, selected as for the recovery archive by
`scripts/recovery-initrd.lua`, the archive's session, network and package
configuration, and Install's `share/installer/system/` files. `full` takes both
trees whole, with every program, the init scripts and `config/live.lua`. Both add
firmware, notices, the recovery archive and Limine's EFI executable and license
for the trusted installer; only `full` accepts the `INIT`, `LIVE_CONFIG`,
`HOSTNAME` and `GO_HELLO` overrides. The SDK is the `pyxis-dev` package, not an
archive tree.

Both archives' `config/packages.lua` names one repository, `public`, at
`PACKAGE_REPOSITORY` ([decisions 65 and 68](../wip/pkg-upgrade.md)).
Development images default to `http://192.168.0.213:5245/public/`;
`make image PACKAGE_REPOSITORY=URL` picks another, and `PACKAGE_REPOSITORY=`
builds an image with no repository. Install, and Rescue on an installed system,
can only fetch `base` for the image's own revision, so a test image needs `base`
packed for it in that repository (`make base-packages REPOSITORY=DIR`). The root Makefile only
orchestrates these steps. New applications belong in userland's install selection; new port payload paths
belong in the ports manifest. Neither requires enumerating archive members in
the root Makefile.

The [installer boot resources](../devices/installer-authority.md#boot-sources-and-handoff)
retain the original ELF and the whole archive as immutable FILE views. They borrow
boot-reserved physical frames through kernel-lifetime read-only/NX mappings,
without copying payloads. The archive view includes final padding and reuses the
reader's existing mapping; it is not a reconstructed collection of members.
The running kernel image is not a substitute for the original ELF file.

## Compressed boot media

Accepted by chronium on 2026-10-29. `build/initrd.cpio.gz` and
`build/caelum.elf.gz` are `gzip -n -6` copies of the archive and kernel,
rebuilt only when their input changes. The ISO, the USB image and the owner's
PXE trees carry only the `.gz` files. Their Limine entries load them through
the pinned Limine's built-in gzip layer (pdgzip, listed in
`third_party/limine/3RDPARTY.md`). Limine applies that layer only to a URI
prefixed with `$`:

```
path: $boot():/boot/caelum.elf.gz
module_path: $boot():/boot/initrd.cpio.gz
```

PXE entries use `$tftp():` the same way. A `$` URI naming an uncompressed file
makes Limine panic with `gzip: decompression error`, and a gzip file without
`$` reaches Caelum compressed and fails the archive check. The `.gz` suffix
keeps the two visibly paired.

Caelum, the archive reader and the installer's boot views receive the
uncompressed bytes, unchanged. The installer therefore writes an uncompressed
`initrd.cpio` and `caelum.elf` with its own plain entry, and the release
package keeps the uncompressed files. Installed disks load from local storage,
where size matters least. `build/caelum.elf` and `build/initrd.cpio` remain the
build's outputs for the release package, bundles and debuggers.

Measured sizes on 2026-10-29: the `full` archive was 75.7 MB, or 28.4 MB
compressed; the 0.0.9 `core` archive 26.2 MB, or 11.3 MB with gzip -9; the
kernel 8.3 MB, or 4.0 MB. gzip's 32 KiB window misses repeats that lie far
apart, such as the rescue programs repeated in the core archive's recovery
archive. zstd's long mode would reach 11.6 and 3.0 MB, but it needs a decoder
in Caelum; that is revisited only if native PXE timing shows the remaining
bytes cost material TFTP time.

## Manifest and publication

A trusted host Lua manifest receives named input paths and returns file/tree
entries with explicit relative destinations:

```lua
{ tree = inputs.userspace, at = "" }
{ file = inputs.init, at = "init", replace = true }
```

`scripts/stage-tree.lua` builds a fresh temporary tree. Directories merge, while
file collisions and file/directory conflicts fail. Only an explicit file entry
with `replace = true` may replace an existing file. Missing inputs, path escapes,
symlinks and special files fail. There are no install hooks, dependency resolution
or package semantics.

The published `build/initrd-root` contains only the current manifest's selections;
removed inputs cannot survive an earlier staging tree. Files use mode 0644,
directories 0755, and timestamps are normalized to the Unix epoch. A sorted,
NUL-delimited list feeds GNU cpio with reproducible numbering and owner/group
zero. An identical tree/archive retains its existing output timestamp, so an
unchanged build does not regenerate the ISO. Failed staging does not replace
the last published tree or archive; the next attempt recreates temporary output.

## Overrides and bundles

`INIT=/path/to/init` explicitly replaces the installed default init. With no
override, the default comes from userland's install tree, including when using
its prebuilt bundle. Game data and the shared font collection are separate
[packages](../../packages/README.md); no image override embeds them. The retired
`DOOM_WAD`, `DOOM_DEMOS`, `QUAKE_DATA`, `DIABLO_DATA` and `DUKE3D_DATA` inputs
are refused when nonempty, with a pointer to the packer recipes.
The image carries no games: Doom, Quake and their Chocolate ports are the `doom`
and `quake` game packages, and EDuke32 and DevilutionX exist only in the owner's
personal `duke3d` and `diablo` packages. The retired `DEVILUTIONX` and `EDUKE32`
flags are refused when set.

[Independent component bundles](build-bundles.md) allow local assembly to
consume completed builds. They do not change the archive format or introduce
runtime mounts. The archive supplies data for [local-time conversion](../userland/timezones.md);
the packaged [session configuration](../userland/session-configuration.md) selects its timezone.
