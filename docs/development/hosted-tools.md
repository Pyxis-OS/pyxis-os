# Native Clang and standalone LLD

The opt-in [payload producer](../../toolchain/README.md#native-hosted-payloads)
builds Clang and LLD against a complete current SDK using the installed Pyxis
compiler. LLVM workers stay off; the real SDK TLS and C++ runtime remain linked.
SDK changes require fresh target objects, not a compiler-container rebuild.
Each bundle contains its own resource/sysroot roots, notices and provenance.
Neither receives a launcher. Driver-invoked linking is a separate task.

## Pool and authority

Prepare a separate npfs volume named `toolchain` on an owner-selected disk.
[Existing mount configuration](../devices/native-readonly-filesystem.md#configure-and-delegate)
selects one GPT disk GUID; volume names and URI strings confer no authority.
Use `MOUNT_DISK=GUID` when assembling the PXE archive. This selects installed
configuration even when booting via PXE: supply `boot://config/installed.lua`,
not `live.lua`. A toolchain-only disk must not inherit stock partition-2
`system`/`home` mounts. For example, a pool in GPT entry 1:

```lua
return {
  hostname = "pyxis",
  volumes = {
    toolchain = { kind = "npfs", partition = 1, volume = "toolchain" },
    home = { kind = "ram" },
  },
  spaces = {
    { name = "remote", title = "Toolchain", init = "boot://init-remote",
      launch = false, remote_power = true,
      roots = { toolchain = "read-only", home = "read-write" }, start = "tmp" },
  },
}
```

Use the full ordinary PXE archive for publication; the installed rescue archive
does not contain the required tar/xfer tools. No compiler payload belongs in
either boot archive. One configured disk supplies the selected volumes; this
does not add a second mounted disk alongside an existing installed-system disk.
Preparing volumes or writing physical media remains an owner action.
Attach the selected qualified USB storage before boot; storage hotplug is not
part of this profile. Never modify a mounted/attached pool externally.

The consumer has read-only `toolchain://`, writable `home://`/`tmp://` and
inherited diagnostics. The manifest declares an 8 MiB guarded stack and
`memory/manage`, `threads/manage`, `clock/read,sleep`, `random/read`.
`app://`, `clang_resources://` and `sysroot://` are delegated read-only from the
selected bundle. Caller-selected roots remain available; this is not confinement
of inherited roots. No raw disk authority or launcher is granted to either tool.

## Manual immutable publication

The producer writes `OUTPUT/hosted-SHA256/payload.tar` and its checksum. Verify
the checksum on the host and upload the archive using existing `xfer receive`.
For publication only, give the publisher `toolchain = "read-write"` and
`launch = true` so a shell script can start tar/sync/mv. The tools' manifests
still withhold the launcher. Consumer spaces remain read-only.

Replace `REV` below with the producer's full `hosted-SHA256` directory name.
Run these as a shell script: its first failed command stops publication.

```text
#!boot://shell.pxe
mkdir toolchain://REV
mkdir toolchain://REV/.staging
cd toolchain://REV/.staging
tar xf tmp://toolchain-REV.tar
sync toolchain://REV/.staging
cd tmp://
mv toolchain://REV/.staging toolchain://REV/payload
sync toolchain://REV toolchain://
```

The first mkdir exclusively reserves the revision. Existing revisions fail
without replacing their files; never reuse a failed or interrupted revision.
The payload child is absent until the complete extracted tree is renamed into
place. Publish its URI only after all syncs succeed. A failed final sync is an
unpublished revision, not permission to delete or overwrite it. Close alone
does not establish durability. This is not a general transactional installer or
power-loss qualification. Cleanly shut down before read-only reopening;
read-only npfs mounts refuse a committed journal needing replay.

Manual per-revision publication is an accepted limitation until the
[parked package manager](../wip/later-os-directions.md#package-manager) is assigned.

## Compile, link, run

Use explicit bundle URIs. A successful compile emits an ordinary ELF object;
standalone LLD emits the runnable P1F/PXE. `-flavor gnu` must be first because
the generic `lld.pxb` launch name does not select the ELF driver.

```text
toolchain://REV/payload/clang.pxb -c tmp://hello.c -o tmp://hello.o
toolchain://REV/payload/lld.pxb -flavor gnu --sysroot=sysroot:// -m elf_x86_64 -static --build-id=none -z max-page-size=0x1000 --oformat=p1f --eh-frame-hdr sysroot://usr/lib/crt0.o -T sysroot://usr/lib/pyxis.ld -Lsysroot://usr/lib tmp://hello.o --start-group -lclang_rt.builtins -lc -lpyxis --end-group -o tmp://hello.pxe
tmp://hello.pxe
```

The bundles enforce the existing 128 MiB selected-image and 256 MiB mapped-span
ceilings. Available physical backing can still refuse capture, image loading or
private allocations. The 8 MiB eager stack is not evidence that arbitrary C++
or deep templates fit. Measurements, failure checks and native qualification
status are in the [task 5 record](experiments/hosted-clang-task5/README.md).
