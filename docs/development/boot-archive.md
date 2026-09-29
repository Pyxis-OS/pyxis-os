# Boot archive assembly

The kernel still reads the same `newc` archive directly from the Limine module.
Guest paths and the kernel reader are unchanged. The build separates compilation,
install trees, guest-tree assembly and archive packing.

Userland's `make install` owns its selected programs, `init` and assets, exporting
`build/userspace-root` through the root build. Objects and debug ELFs stay in
`build/userspace`. Ports owns `ports/install.lua`, which selects its executables,
licenses and TCC support files into `build/ports-root`. Host port tools stay out
of the guest tree.

`boot/initrd.lua` combines those trees with the SDK's target headers, runtime
archives, toolchain notices and provenance. It adds the selected Doom data and
optional init override. The root Makefile only orchestrates these steps. New
applications belong in userland's install selection; new port payload paths
belong in the ports manifest. Neither requires enumerating archive members in
the root Makefile.

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
its prebuilt bundle. `DOOM_WAD` selects a local retail/custom WAD, and
`DOOM_DEMOS` selects the directory containing the two supported demo files.
Without the WAD override, package the vendored shareware WAD and its notices.
Removing overrides removes their old payloads on the next assembly.

[Independent component bundles](build-bundles.md) allow CI or local assembly to
consume completed builds. They do not change the archive format or introduce
runtime mounts. The archive supplies data for [local-time conversion](../userland/timezones.md);
the packaged [session configuration](../userland/session-configuration.md) selects its timezone.
