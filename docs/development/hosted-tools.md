# Native Clang and LLD

The opt-in [payload producer](../../toolchain/README.md#native-hosted-payloads)
builds Clang and LLD against a complete current SDK using the installed Pyxis
compiler. LLVM workers stay off; the real SDK TLS and C++ runtime remain linked.
SDK changes require fresh target objects, not a compiler-container rebuild.
Each bundle contains its own resource/sysroot roots, notices and provenance.
The driver profile embeds a leaf LLD bundle under Clang's read-only `linker://`
root. Clang's manifest requests optional `launcher/launch`; withholding it keeps
compile-only available and linking refuses. LLD never receives a launcher.
The native first compile, standalone link and PXE run passed on the ThinkPad
on 2026-10-13, including rejected inputs/outputs and matching object/PXE hashes.

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
  },
  spaces = {
    { name = "network", title = "Network", init = "boot://init", network = true },
    { name = "remote", title = "Toolchain", init = "boot://init-remote",
      launch = true, remote_power = true,
      roots = { toolchain = "read-only" }, start = "tmp" },
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

The consumer has read-only `toolchain://`, ordinary writable `tmp://` and
inherited diagnostics. The manifest declares an 8 MiB guarded stack and
`memory/manage`, `threads/manage`, `clock/read,sleep`, `random/read`.
`app://`, `clang_resources://` and `sysroot://` are delegated read-only from the
selected bundle. Clang additionally receives read-only `linker://`; its optional
launcher is supplied only when the space delegates launch. Caller-selected roots
remain available; this is not confinement of inherited roots. Neither tool
receives raw disk authority. Set `launch = false` for a compile-only consumer.

## Manual immutable publication

The producer writes `OUTPUT/hosted-SHA256/payload.tar` and its checksum. Verify
the checksum on the host and upload the archive using existing `xfer receive`.
For publication only, give the publisher `toolchain = "read-write"` and
`launch = true` so a shell script can start tar/sync/mv. The tools' manifests
declare only optional launch for Clang; LLD stays a leaf. Consumer volume roots
remain read-only.

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

With the driver-profile revision and declared launch authority, the ordinary
single-source command is:

```text
toolchain://REV/payload/clang.pxb tmp://hello.c -o tmp://hello.pxe
tmp://hello.pxe
```

Clang runs cc1 in-process, then prepares its declared `linker://lld.pxb` through
the shared bundle loader and contained native job core. It owns the subgroup's
CONTROL until the real child result and subtree cleanup complete. Closing Clang
on exit/fault stops its surviving job subtree; no wrapper, broker or executor
supplies authority. LLD inherits diagnostics and explicitly selected content
roots/cwd/environment, its own manifest runtime grants and 8 MiB stack, and no
launcher. An uncertain admission is never retried. Multiple-source/external-cc1
execution and unsupported statistics/timeouts are outside this profile.

## Qualified capacity

The bundles retain the existing 128 MiB selected-image and 256 MiB mapped-span
ceilings. Current artifacts and the native small-C qualification give:

| Per tool | Clang | LLD |
| --- | ---: | ---: |
| Serialized image bytes | 90,347,866 | 55,743,570 |
| Page-rounded image backing/span bytes | 90,939,392 | 56,352,768 |
| Eager guarded stack | 8 MiB | 8 MiB |
| Temporary image-capture backing bytes | 90,349,568 | 55,746,560 |
| Native peak owned VM backing, reported decimal MB | 101.85 | 70.23 |
| Provisional owned-data planning allowance | 128 MiB | 96 MiB |

Planning allowances include headroom above the qualified peaks; they are tunable
policy, not new enforced quotas or bounds on arbitrary inputs. Peak owned
backing already includes image, stack, startup and private working allocations.
Capture is a separate kernel allocation released around startup; independent
peaks are not a measured simultaneous total. Page tables, kernel metadata/cache/
stacks, boot archive, device/borrowed mappings and scratch files need separate
space, so this is not a minimum system-RAM specification. Available backing can
still refuse capture, image loading or private allocations honestly.

The 8 MiB eager stack is not a measured high-water bound for deep includes or
C++ templates. Native wall proxies were about 4.29 s for Clang version,
4.33 s for the small compile and 2.97 s for LLD, dominated by repeated USB image
loading. [Loading/reuse debt](../technical-debt.md#hosted-tool-launch-loading)
is deferred. Samples, probe exclusions, failure checks and native evidence are
in the [task 5 record](experiments/hosted-clang-task5/README.md).
