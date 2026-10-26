# Native Clang and LLD

The opt-in [payload producer](../../toolchain/README.md#native-hosted-payloads)
builds Clang, LLD and [clangd](#clangd) against a complete current SDK using the
installed Pyxis compiler. LLVM workers stay off for Clang and LLD, and clangd
builds in a separate threads-on tree; the real SDK TLS and C++ runtime remain linked.
Hosted sources are pinned independently of the installed cross compiler. Reuse
the same persistent Ninja work directory for development: SDK changes refresh
configuration checks and rebuild affected dependencies/relink, while source
updates rebuild affected files. Compiler/tool or configuration changes trigger
a reported clean rebuild; `build-hosted.sh --clean` requests one explicitly.
Neither SDK changes nor hosted-only changes require a compiler-container rebuild.
See the [producer commands](../../toolchain/README.md#native-hosted-payloads).
Each bundle contains its own resource root, notices and provenance, and reads
the SDK from `sdk://`, the installed `pyxis-dev` and `libcxx-dev` packages
([`sdk://`](../interfaces/packages.md#reaching-package-files)), on which
`clang-hosted` depends.
The driver profile embeds a leaf LLD bundle under Clang's read-only `linker://`
root. Clang's manifest requests optional `launcher/launch`; withholding it keeps
compile-only available and linking refuses. LLD never receives a launcher.
The first compile, standalone link and PXE run, and then the one-command
driver link, passed on the ThinkPad on 2026-10-13
([native driver results](#native-driver-results)). Both retain honest
diagnostics and failure results.

## Package installation

Configure a network repository with [`pkg`](../interfaces/packages.md#package-client)
in a space with package-management authority, then run:

```text
pkg install clang-hosted
```

The producer writes a sibling `clang-hosted.lua` recipe beside `payload/` and
`payload.tar`. The ordinary host packer uses it to publish the immutable package
and explicitly declares six commands: `clang`, `clang++`, `clang-cpp`, `lld`,
`ld.lld` and `clangd`. They select the real `clang.pxb`, `lld.pxb` and
`clangd.pxb` bundles at the package tree's top level, retaining each bundle's
manifest, stack and resource policy. There is no wrapper process or separate
alias manifest. Clang's invoked name selects its C, C++ or preprocessor mode;
`ld.lld` selects ELF, while generic `lld` requires `-flavor gnu`.

Bare tool names resolve through the common live package-command entry in the
[command list](../interfaces/command-search.md). Installing or switching a package
makes its exports available to subsequent launches in an existing space; a
running tool keeps its selected bundle and roots. No fixed `toolchain://` space
root or per-program package catalog grant is required. Missing exports refuse
launch. Neovim's default launches bare `clangd` and obtains resource views from
the actual admitted server selection.

Live installs use the volatile packages pool and last until reboot; there is no
archive fallback or local seed. A download and extraction need room for both
copies, retained versions and application memory; the live pool's logical
capacity is not a RAM reservation. The native compiler and clangd measurements
below used manual publication. Package delivery was qualified separately in
[QEMU](experiments/packages-slice1/README.md#hosted-toolchain-package-2026-10-20).

The explicit manual deployment/recovery workflow below remains available for an
owner publishing a producer revision outside packages. It requires a separately
configured `toolchain://` volume root.

## Pool and authority

For manual publication, prepare a separate npfs volume named `toolchain` on an
owner-selected disk.
[Existing mount configuration](../devices/native-readonly-filesystem.md#configure-and-delegate)
selects one GPT disk GUID; volume names and URI strings confer no authority.
Use `MOUNT_DISK=GUID` when assembling the PXE archive, and describe the disk in
`boot://config/live.lua`, which such a build reads. A toolchain-only disk must not inherit stock partition-2
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
`memory/manage`, `threads/manage`, `clock/read,precise,sleep`, `random/read`.
`app://` and `clang_resources://` are delegated read-only from the selected
bundle; each bundle's `"sdk": true` takes `sdk://` from the space's packages
source, or from the launching bundle's own `sdk://`, as Clang passes it to LLD.
A space without the packages volume and `pyxis-dev` has no SDK to compile
against. Clang additionally receives read-only `linker://`; its optional
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

For ordinary installation, use the `clang-hosted` package instead of this
manual workflow. Repository production and deployment are tracked in
[packages slice 1](../wip/packages.md#slice-1-data-packages).

## Compile, link, run

Use the installed package's bare command exports. A successful compile emits
an ordinary ELF object; standalone LLD emits the runnable P1F/PXE.
`-flavor gnu` must be first for generic `lld`; the `ld.lld` export selects the
ELF driver without that argument.

```text
clang -c tmp://hello.c -o tmp://hello.o
lld -flavor gnu --sysroot=sdk:// -m elf_x86_64 -static --build-id=none -z max-page-size=0x1000 --oformat=p1f --eh-frame-hdr sdk://lib/crt0.o -T sdk://lib/pyxis.ld -Lsdk://lib tmp://hello.o --start-group -lclang_rt.builtins -lc -lpyxis --end-group -o tmp://hello.pxe
tmp://hello.pxe
```

With the driver-profile revision and declared launch authority, the ordinary
single-source command is:

```text
clang tmp://hello.c -o tmp://hello.pxe
tmp://hello.pxe
```

For the explicitly configured manual volume described above, use
`toolchain://REV/payload/clang.pxb` or `toolchain://REV/payload/lld.pxb`
instead of the bare name. Those paths select that held root's published revision
and do not follow the package set.

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
ceilings. Current driver-profile artifacts and the separately identified task5
native baseline give:

| Per tool | Clang | LLD |
| --- | ---: | ---: |
| Current serialized image bytes | 90,385,074 | 55,776,366 |
| Current page-rounded image backing/span bytes | 90,976,256 | 56,389,632 |
| Eager guarded stack | 8 MiB | 8 MiB |
| Current image-capture backing bytes | 90,386,432 | 55,779,328 |
| Task5 native peak owned VM backing, historical decimal MB | 101.85 | 70.23 |
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
loading. [Loading/reuse debt](https://git.internal/PyxisOS/pyxis-os/issues/1384)
is deferred. Samples, probe exclusions, failure checks and native evidence are
in the [task 5 record](experiments/hosted-clang-task5/README.md).

The current driver [qualification](experiments/hosted-clang-driver/README.md#qualified-driver-integration-2026-10-13)
measures 101,888,000/70,451,200 bytes per compiler/linker VM in QEMU, and an
actual simultaneous VM+capture peak of 238,833,664 bytes including other guest
processes. Cleanup returns each VM and capture to zero. No native driver peak
was measured; this total excludes kernel/cache and is not a minimum-RAM claim.
The probe that produced the peaks stays unmerged and its images are never staged
as ordinary builds.

The allocator grows TLSF pools from eager zeroed private memory, at least 64 KiB
each with slack for large requests; there is no fixed pool count or measured
total-heap limit, and a single request above 2 GiB fails. Pools are retained
until exit and `realloc` growth allocates and copies before freeing. Threads-off
`RunSafelyOnNewStack` runs inline. Large C/C++/LTO inputs, malloc exhaustion in a
started tool and arbitrary stack depth remain unqualified; refusal of an
oversized capture or image was checked in QEMU only.
A failed ordinary link preserves pre-existing requested output and leaves no
partial output; the driver cleans its owned temporary files.

## clangd

`clangd.pxb` sits beside `clang.pxb` and `lld.pxb` in the same immutable package,
exported as bare `clangd` through the live package-command view. It carries the same
`clang_resources` read-only root, notices and provenance, reads `sdk://`, and
requests the same `memory`, `threads`, `clock` and `random` grants with an 8 MiB
stack. It requests no launcher: clangd starts no programs. The consumer supplies
the project roots, which must allow writing only if background indexing is used.

clangd needs LLVM threads, so it builds from the same fork commit in its own
tree (threads on, no clang-tidy checks, static analyzer, ARCMT, remote index or
XPC); Clang and LLD are built exactly as before. Libc's detached threads
([threads](../userland/threads.md#creation-and-collection)) back its workers.
Flags, as the [Neovim setup](../userland/neovim.md) passes them:
`--resource-dir=clang_resources:// --pch-storage=memory
--background-index=false -j=N`. clangd cannot count processors on Pyxis, so the
default is one worker; the `-sync` mode is the fallback if threads misbehave.
Paths must be native (`home://proj/main.c`); the LSP side maps them to
`file:///home/proj/main.c` as decided for the milestone.

Historical producer/manual-publication measurements on the 2026-10-17 payload
(fork `abc00d0416be`, QEMU/KVM for the run below; sizes are producer output),
before package-root delivery:

| | clangd |
| --- | ---: |
| Serialized image bytes | 44,408,146 |
| Page-rounded image backing/span bytes | 44,630,016 |
| Eager guarded stack | 8 MiB |
| Bundle directory bytes (image, resources, sysroot, notices) | 72,746,852 |

The payload archive grows from 293,416,960 to 369,326,080 bytes, so a revision
needs about 76 MB more on the pool. The producer took 25 min 8 s at `-j10` on a
shared 12-CPU VM (clangd's second tree is most of the increase); Clang and LLD
images are within 2 KB of the earlier numbers (90,387,062 and 55,778,250 bytes).
`clangd --version` and an LSP session through a pipe — initialize, didOpen,
diagnostics, hover, definition, completion, formatting, a code action inserting
an include, shutdown — ran from the staged bundle, in threaded and `-sync`
modes, with `compile_flags.txt` and `compile_commands.json` relative flags found
from outside the project root. No peak memory was measured.

Limits (also in [technical debt](https://git.internal/PyxisOS/pyxis-os/issues/1382)):
no preamble, so every edit reparses its headers; each open file holds two 8 MiB
eager thread stacks and the process allows 64 threads; there is no user config or
cache directory, no `--check`, and no signal or process-ID features.

## Process boundary and driver contracts

Default integrated cc1 and integrated assembly run one single-source `-c` in
process. Multiple jobs or process statistics disable cc1 integration, and the
Pyxis toolchain constructs an external LLD command instead of embedding LLD. The
fork disables child crash helpers and recovery, so a fatal compiler or library
fault ends the process. LLVM worker threads stay off, but the real runtime
primitives and static TLS are linked ([native threads](../interfaces/threads.md)).
Objects and archives stay ELF and LLD writes P1F directly.

The driver follows these owner-accepted contracts (2026-10-13):

- **Containment.** A contained job profile extends the [native job core](../userland/native-jobs.md):
  Clang holds sole, non-forwarded CONTROL of a private subgroup for its LLD child.
  Release on Clang's exit or fault requests subtree stop. Normal completion waits
  for process retirement and subgroup cleanup, and unknown admission is never
  replayed. `PROCESS_WAIT` stays process-only, so on a Clang fault its observer can
  complete before LLD's deferred cleanup; the parent group's completion is the
  transitive observation.
- **Creation.** Scoped group construction comes from LAUNCH with bounded trees, as
  described in [execution groups](../interfaces/execution-groups.md#creation-and-launch-authority).
  The LLD leaf deliberately receives no launcher.
- **Bundle profile.** One self-contained Clang bundle with a declared read-only
  `linker://` root holding `lld.pxb`. Clang's optional `launcher/launch` keeps
  compile-only usable when withheld; linking then refuses honestly. The LLVM
  adapter is a thin core client using the shared SDK bundle preparation, explicit
  working-directory and environment snapshots and inherited descriptors.
  Statistics and other unsupported policies remain explicit refusals, and cc1
  integration, temporary-file cleanup and honest exit, fault and termination
  results are preserved. A failed link no longer deletes a requested output that
  already existed.
- **Memory.** Image and span ceilings and stacks are unchanged; overlapping
  Clang and LLD and capture backing are measured, not summed from independent peaks.

Nothing in these generic groups, jobs and manifests carries x86 register, APIC or
ELF-machine policy, and executable selection stays on the common launcher and
loader path, with no foreign syscalls, `fork` or per-space compiler setting. See
[subsystems per space](../wip/subsystems-per-space.md).

## Delivery

| Step | Pull requests | Record |
| --- | --- | --- |
| LLVM Support platform and header fixes | fork 4, #708 | [LLVM Support](llvm-support.md), [record](experiments/hosted-clang-support.md) |
| SDK C++ source closure | userland #216, fork 5, #714 | [C++ in userspace](cxx-userspace.md), [record](experiments/hosted-clang-cxx.md) |
| Remaining reached libc operations | userland #217, #718 | [record](experiments/hosted-clang-libc.md) |
| Thread-gated Clang/LLD integration | fork 7, userland #242, #763 | [record](experiments/hosted-clang-integration.md) |
| Installed tools: pool bundles, compile, separate link, run | #772 | [task 5](experiments/hosted-clang-task5/README.md) |
| Nested subgroup foundation | #789, userland #248 | [driver record](experiments/hosted-clang-driver/README.md#native-subgroup-foundation-2026-10-13) |
| Contained core and one-command driver | fork 8, userland #251, #793 | [driver record](experiments/hosted-clang-driver/README.md#qualified-driver-integration-2026-10-13) |
| clangd bundle | fork 10, 13, 11; userland #288; #927, #929 | [clangd in Neovim](../userland/clangd.md) |

The installed cross compiler and SDK runtimes follow the pin contract in
[LLVM toolchain](llvm-toolchain.md#changing-the-pin); the driver adopted
`pyxis-llvm23.1.3-e0801c2`. Hosted sources are now pinned separately in
`toolchain/build-hosted.sh`. Rebuilding LLVM inside Pyxis is a later result with
its own build tools and dependencies (see [source builds](../wip/source-builds.md)).
General compiler/build-tool descendant chains still require the separate nested
launch policy and qualification; making commands searchable does not supply that
authority or qualify native builds.

## Native driver results

Owner session, 2026-10-13, ThinkPad T14 Gen 1 AMD booted over PXE with the
write-through USB build and the toolchain stick, read-only for the consumer.
Publication of the 293,416,960-byte payload verified its SHA-256, and the extract
took 332.9 s.

- `clang --version` reports 23.1.3 from the fork.
- `clang hello.c -o hello.pxe` exits 0 and the PXE prints `hello 5`, as does the
  separate `-c` plus standalone LLD path. Object and PXE hashes match the
  expected values exactly.
- Failures exit 1 honestly and leave no output: a syntax error with caret and
  fix-it, an undefined symbol reported through the LLD failure, an unknown linker
  option, output into the read-only volume (Permission denied),
  `-fproc-stat-report` (statistics unavailable) and an oversized response file
  (capture capacity).
- An existing output stays byte-identical after a failed explicit and a failed
  default link.

Not run natively: the launcher-withheld configuration and the memory probe's peaks.
Writing the 280 MiB payload ran at about 0.9 MB/s, far below what the stick should
sustain; this is the write-side counterpart of
[bounded read aggregation](../devices/usb-storage.md#bulk-read-aggregation) and
is recorded under [later directions](../wip/later-os-directions.md#persistent-storage-and-installation).
Selected-image reuse remains [technical debt](https://git.internal/PyxisOS/pyxis-os/issues/1384).

