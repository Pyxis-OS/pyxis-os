# Boot archive assembly

Agreed build-system milestone after [UTC wall-clock support](wall-clock.md),
before adding the full timezone database. Preserve the current `newc` boot
archive, guest paths and kernel reader.

## Assembly contract

Separate building outputs, assembling the guest tree and packing the archive.
Userspace and ports build their artifacts; an explicit host-side Lua manifest
selects files and directory trees into a dedicated `build/initrd-root` staging
directory. GNU cpio packages the completed tree. Host Lua is already a build
dependency and does not depend on the later guest Lua port.

The small manifest needs file and tree inclusion with explicit destination
paths. It is not a package manager, dependency solver or plugin framework.
Userspace should provide an install tree of selected programs and assets,
excluding intermediate objects and debug ELFs. Port executables, assets and
licenses should be included without repeating each archive member in the root
Makefile. Preserve existing paths such as `app://cat.pxe`.

Keep INIT, retail WAD and demo overrides explicit. Recreate staging so removed
inputs cannot survive from an earlier build. Merge directories but reject
conflicting file destinations; replacements such as the selected init must be
intentional. Missing required inputs fail the build. Use stable ordering and
metadata so unchanged inputs leave the archive unchanged, and only replace the
published archive when its contents change.

## Focused tasks

- [ ] Settle the manifest shape and install-tree ownership across Pyxis,
  userspace and ports; keep policy in the repository that owns each component.
- [ ] Add the userspace install tree and assembly manifest/runner; migrate the
  current programs, licenses, guest SDK and Doom data without changing guest paths.
- [ ] Reduce the root Makefile to build/assemble/pack orchestration. Check ordinary
  builds, unchanged rebuilds, override selection/removal and normal boot manually.

Exact filenames and helper interfaces remain implementation choices. Avoid
adding another generic build system alongside Make and the port recipes.
