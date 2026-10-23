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
