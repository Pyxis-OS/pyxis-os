# Init, SDK, ports and device-backed filesystems

Status: working discussion after the first-shell milestone. User directions are
recorded below; suggestions and questions remain provisional. This is not an
implementation assignment. Prepare the documentation PR after the boundaries
are sufficiently settled, and split implementation into focused tasks afterward.

## Recorded direction

- Replace direct shell boot with a selected init, which may be a native executable
  or an interpreter script. Start with primitive shell scripting.
- Allow a Make override for init, including a local temporary script useful for
  manually exercising a program. This does not itself authorize a test harness,
  boot/output automation or new CI checks.
- Init should eventually perform setup such as mounting a VirtIO filesystem.
- Create separate userspace and ports repositories. Initially pin them as
  submodules and orchestrate their builds from Pyxis. The owner will configure
  dispatch-triggered userspace CI and cross-repository workflow integration.
- Describe ports in Lua: a catalog, one directory per port, source metadata with
  pinned revisions, an ordered patch set, and a build.lua recipe.
- Produce the kernel ELF, ISO and an SDK usable by userspace and ports. Build the
  OS-specific compiler into the builder container, never in ordinary workflows.
  The owner builds/rebuilds that container when requested; routine SDK/header
  updates should not require rebuilding it.
- Candidate ports: Lua, Kilo, TCC, SQLite, Doom, a CHIP-8 interpreter, “fortz”
  (confirm whether Frotz was intended), and NetHack. Earlier goals such as Neovim
  and serving the project's website remain later directions.
- Develop PCI/VirtIO support in the order virtio-fs, virtio-net, then virtio-blk
  (confirmed by the owner). Disk filesystem format selection and an installer
  are later candidates.
- Consider a FUSE-inspired filesystem interface and a host FUSE implementation
  if a custom disk format is chosen.

The newly merged .forgejo/workflows/build.yml builds the kernel/ISO in the image
from ci/Containerfile and publishes both artifacts. It has push, PR and manual
triggers. The container currently supplies the generic x86_64-elf compiler.
No container, workflow, repository or submodule changes are part of this draft.

## Small shell follow-up

The unconditional newline after every foreground child is annoying. Replace it
with starting the next prompt on a fresh line only when needed. Preserve child
output that ends mid-line: libterm currently clears the prompt row. The terminal
owns cursor state, so guessing from the shell's own writes is insufficient.
The specific query or terminal operation remains to be selected separately.

## Init and script execution

Proposed scope: replace the current application-space entry on CPU 1 (BSP on a
single-CPU boot), retaining Caelum's separate role. This does not define a global
Unix PID 1, complete supervision, or the future per-space lifecycle model.

Keep boot material in the initrd even after virtio-fs exists: init and its
interpreter must be readable before init can mount anything. The selected entry
must be explicit, with ordinary failure diagnostics and no silent fallback to
another executable.

A possible build interface is `INIT=<host path>`: stage those bytes under a
stable archive entry, without editing tracked source. Distinguish the build-host
path from a guest capability path. Exact Make names, a default script, optional
arguments and placement in the userspace repository are still open. Changing
an override must invalidate the relevant build outputs, even if the new file
has an older timestamp. A temporary init must not leak into subsequent default
builds or published artifacts.

Shebang handling should be a program-launch facility reusable beyond the
interactive shell. Interpreter lookup must stay within explicitly supplied
roots; knowing an interpreter URI does not grant access. Avoid growing a general
path parser inside the kernel. The boot adaptation and userspace launch helper
need a concrete design because today's loader accepts an opened image handle.

Decide the initial shebang grammar: explicit interpreter URI, whether an optional
argument is accepted, first-line length bound, CRLF handling and recursion/cycle
policy. Prefer a bounded, nonrecursive first implementation. An interpreter must
receive authority to read the script, not merely a pathname. Passing the already
opened READ capability avoids requiring a second lookup; argv convention and the
resource name need agreement.

Proposed shell script mode reuses current quoting and command execution: one
command per line, blank lines and whole-line comments, cd and foreground launch.
No expansion, variables, conditions, loops, pipelines or redirection initially.
Process the final line without a newline and report script name/line on errors.
Do not silently truncate long lines. Proposed boot-script policy is to stop on
syntax, cd, launch, nonzero child exit or child fault; confirm this rather than
implicitly adopting interactive-shell behavior. Script EOF exits without an
interactive prompt unless a handoff is explicitly requested.

Init needs setup authority that ordinary applications should not inherit.
Current shell children intentionally receive no launcher, so simply putting
`shell` in a script cannot launch a fully functioning interactive shell today.
Choose an explicit handoff/delegation mechanism, including eventual mount
management authority, before implementing startup scripts. Also settle whether
init waits, exits after launching a session, or becomes the session program,
and what should remain visible after startup failure or normal init exit.

## Repositories, SDK and compiler

Proposed ownership:

- Pyxis: kernel, exported native ABI/format headers, boot/image assembly,
  toolchain patches/container recipe and pinned integration revisions.
- Userspace: libc, libpyxis, libterm, startup/link support, native applications
  and initial boot scripts. Exact ownership of elf2pxe remains open.
- Ports: pinned third-party source descriptions, patches, recipes and staging
  rules; consume an explicit SDK rather than internal kernel source paths.

Keep public ABI headers authoritative in one repository and export them into
the SDK. Do not move kernel-private headers into the sysroot or maintain manual
copies across repositories. Existing consumers remain under project control:
no compatibility shims or automatic ABI-version increments are implied.

A proposed build order avoids a userspace/SDK dependency cycle:

1. Use the prebuilt compiler and export target headers into a staging sysroot.
2. Build userspace runtime/startup and libraries against those headers.
3. Export the complete SDK: public headers, startup objects, libc/native/terminal
   archives, linker support and the host executable converter.
4. Build applications and selected ports against that SDK, stage their outputs,
   and assemble the boot archive/ISO alongside the kernel.

The same userspace checkout may provide separate runtime and application build
phases; this need not become another repository. Whether libc/native runtime
instead stay with the kernel is a decision, not an implementation detail.

Keep the compiler binary and evolving target sysroot separate. Configure the
compiler for an external sysroot and supply the selected SDK to each build.
A candidate target is x86_64-unknown-pyxis, with __pyxis__, appropriate startup
and linker defaults, a defined C data/calling convention and target libgcc.
A target name alone is insufficient. Port code must use only the runtime and
machine features Pyxis actually supports.

Continue using ELF objects, indexed .a libraries and a debug ELF followed by
ELF-to-PXE conversion initially. Native PXE binutils support, dynamic linking
and custom library formats need separate justification.

Ordinary header/library/linker-script/converter changes should ship in the SDK.
Compiler/binutils patches, target conventions, compiler runtime configuration
or required host dependencies may require a container rebuild. Record exact
source revisions and required compiler identity with SDK artifacts; this is
build provenance, not a new compatibility-versioning policy. Integration and
future dispatch jobs should select an exact SDK and source revisions, rather
than racing an unqualified latest artifact. The owner controls dispatch setup
and container publication.

## Ports and host Lua

Proposed minimal shape: a ports.lua catalog naming available ports; each port
has a metadata table, build.lua and an explicit patch order. Metadata includes
source URL, exact commit (or release archive plus checksum), license, dependencies
and installed outputs. Separate host build dependencies from Pyxis libraries.
Choose one source of truth for version/dependency data rather than duplicating
it in the catalog and recipe.

Use a host Lua interpreter for recipes; this does not depend on first porting
Lua into Pyxis. Start with a small runner supplying toolchain/sysroot, source,
build and destination directories. Recipes can invoke the upstream build system.
Build into a staging directory rather than overwriting the consumed SDK. Package
format, dependency resolver sophistication and binary distribution are separate
questions; do not invent them all for the first port.

Suggested first consumers are Lua and Kilo, to expose concrete libc, terminal
and file-operation gaps. Lua configuration evaluation and a full Lua program
need not receive identical libraries/authority. Current stdio formatting lacks
floating point; runtime/math and file support should be assessed against the
selected upstream revision. SQLite, TCC, graphical ports and larger terminal
applications each need their own platform contract review, not POSIX-shaped
kernel syscalls added by default. No priority order among the candidates is fixed.

## VirtIO, mounts and persistent storage

A likely dependency order is PCI discovery, modern VirtIO PCI transport,
virtqueues/DMA ownership/completion handling, then a small virtio-fs client.
A read-only shared-directory slice can establish lookup/enumeration/read before
writable coherency, DAX or performance work. Select the supported protocol and
operation subset explicitly when defining that milestone.

Virtio-fs carries FUSE requests to a host filesystem service. It gives useful
host access without choosing an on-disk format or first implementing networking.
It still needs concrete directory/file backend integration and mount exposure.

Mount management should be an explicit authority held by init, not granted to
any process that knows a mount name. Decide whether the first operation binds a
new scheme root (for example host://), attaches beneath an existing directory,
or both. Define namespace ownership, visibility to existing processes, lifetime
of open handles and unmount behavior. Today's startup roots are copied bindings,
so mounting in init does not automatically update previously launched processes.
Mount-before-launch can keep the first slice simple.

Keep three choices separate: Pyxis file/directory capability requests, a backend
operation interface, and the disk format. A FUSE-inspired backend need not force
Linux FUSE's complete wire ABI, Unix permissions or path semantics on applications.
If a custom disk format is selected, a freestanding format implementation could
be shared by a Caelum adapter and a Linux FUSE adapter. The host and kernel sides
would supply their own I/O/allocation glue. The native filesystem need not run in
userspace to enable host mounting.

Defer existing-versus-custom disk format selection and installer design until
there are block I/O and concrete persistence requirements. Virtio-fs can support
port development while those decisions remain open.

## Next discussion points

- Is init a one-shot setup program followed by a shell, or a resident supervisor?
  Confirm the application-space scope and script failure/handoff policy.
- Which repository owns libc/native runtime, exported ABI headers and converter?
  Agree on the staged SDK build before splitting repositories.
- Confirm the initial port priorities and whether Frotz was intended above.
- Settle init launch/delegation and mount namespace ownership before code work.

## References

- [Current compiler container](../../ci/Containerfile) and
  [artifact workflow](../../.forgejo/workflows/build.yml).
- [GCC sysroot selection](https://gcc.gnu.org/onlinedocs/gcc/Directory-Options.html).
- [Virtio-fs design and FUSE transport](https://virtio-fs.gitlab.io/design.html).
- [Lua embedding and standard libraries](https://www.lua.org/manual/5.4/manual.html).
- [Existing development candidates](development-paths.md),
  [filesystem direction](../vfs.md) and [space direction](../spaces.md).
