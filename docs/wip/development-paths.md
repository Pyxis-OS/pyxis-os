# Possible development paths

Status: working notes from discussion, not an approved design or ordered task
list. These are candidates to revisit and split into focused milestones. No
implementation is assigned by this document.

The [shell and runtime reference](../first-shell.md) describes the implemented
filesystem, application runtime and libraries selected from these candidates.
The remaining paths below stay parked for later discussion.

Request/reply endpoints, request-side capability copies and BSP-serviced
capability-table growth are implemented. Their handoff is recorded in
[the process/capability worklist](process-capability-abi.md).

## Kernel and resource-interface candidates

1. **Reply-side capability transfer.** Let a service return a newly granted
   resource. A small example could resolve an initrd filename and return a READ
   file capability. This supports resource discovery without first settling a
   complete VFS or naming model.

2. **Userspace process launching.** An authorized program supplies an image
   capability and initial grants, starts a child, and observes its exit. This
   begins the supervisor role described in the [spaces draft](../spaces.md).
   The initial design can retain one task per process and pinned CPUs. Launch
   authority, startup resources, arguments and exit observation need discussion.

3. **Framebuffer access through a capability.** Allow a userspace program to
   draw into its space's framebuffer through an explicit grant. Begin with a
   small graphical program and work toward Doom. Decide pixel formats, rights,
   and copied drawing requests versus mapped memory.

4. **Input delivery to userspace.** Keep global space-navigation shortcuts under
   the session interface and deliver application keyboard events to the active
   space. A key-event viewer would exercise focus, blocking, event ownership and
   queue limits. The input protocol remains open.

5. **Memory objects and shared mappings.** Represent memory with handles that
   can be mapped and shared explicitly. This supports large IPC payloads and
   graphics. Define mapping permissions, lifetime and accounting, while
   respecting the current VM concurrency constraints.

6. **PCI discovery and VirtIO networking.** Begin with PCI enumeration, then
   device resources, interrupts, DMA and a network device. This advances the
   static-website hosting goal and eventually connects to driver authority in
   the capability model.

The earlier suggested sequence was reply-side transfer followed by a small
initrd lookup service, then either process launching or framebuffer/input work.
That sequence is parked for now, not committed. Move semantics remain a separate
candidate when a concrete ownership-transfer use appears.

## A usable userspace environment

A proposed practical milestone is: boot into a shell, browse files, launch
programs, edit a file, and run a Lua script. The kernel candidates above remain
supporting work; this list gives them concrete applications to serve.

1. **VFS and namespaces.** Develop the [filesystem draft](../vfs.md), including
   the shared read-only system base, per-space overlays, publishing overlay
   changes to the shared base, and a common writable home area. Scheme-based
   names such as `app://` and `home://` remain part of the direction.

   One proposal is to resolve a scheme to a directory capability in the
   process's namespace, with lookup returning further directory/file grants.
   Names would not confer authority. The first-shell milestone selects
   kernel-resident directory/file objects with path resolution in the native
   userspace library.

   A first prototype could expose an initrd-backed read-only tree and a writable
   RAM-backed tree, enough for listing, reading, creating directories and saving
   edits. Discuss lookup, enumeration, offsets, creation, deletion and rename.
   Disk persistence, overlay merging and a new on-disk format need not be part
   of that prototype.

2. **Pyxis SDK, toolchain and library formats.** Assemble startup code, ABI
   headers, native libraries, libc, linker defaults and image conversion into a
   coherent development environment. Consider an OS-specific compiler target
   and teaching binutils the Pyxis format when their concrete requirements are
   clearer.

   The original format ideas were an archive of unlinked objects and a
   symbol-bearing format for static libraries. The initial recommendation is
   ELF relocatable objects plus ordinary indexed `.a` archives, linked to ELF
   and then converted to PXE. This already supports static libraries, including
   future core or networking libraries. Custom object/library formats remain an
   option, but would also require relocation and symbol-resolution rules.
   The final executable format does not require matching custom build formats.

3. **Native runtime, libc and allocation.** Consider a small `libpyxis` exposing
   native capabilities and operations, with a C library layered over those
   facilities. Familiar C file and allocation interfaces need not dictate POSIX
   kernel syscalls. Compare existing libc candidates and their platform hooks
   before choosing one.

   Reuse an existing allocator if suitable. First define how userspace obtains
   backing memory; the allocator manages that memory inside the process,
   independently of the kernel heap. Runtime startup, errors, termination and
   the services required by the first ports belong in this discussion too.

4. **Shell, utilities and editors.** Start with a purpose-built shell that reads
   commands, parses arguments, resolves and launches programs, waits for them,
   and reports results. Pipes, redirection and advanced shell behavior are not
   needed initially. Add utilities such as `ls`, `cat` and `mkdir` to exercise
   the filesystem interfaces.

   Kilo is a possible first editor, requiring file access, allocation, keyboard
   input, cursor control and saving. Adapt its terminal/file interfaces to
   Pyxis. Neovim is the first intended substantial editor port and a later
   platform milestone; its libuv and LuaJIT dependencies need separate planning.

5. **Lua for configuration and programming.** Explore Lua as the common
   configuration language instead of JSON, TOML or YAML, and provide a small
   interactive REPL. A starting convention could be configuration scripts that
   return tables, with selected Pyxis functions exposed where useful.

   Share the language and libraries while giving evaluators their own Lua
   states and explicit resources. Decide available libraries and authority for
   configuration execution. The first Lua port and Neovim's Lua runtime
   requirements should be evaluated separately.

The implemented filesystem and application-runtime contracts are described in
[the shell and runtime reference](../first-shell.md). The shell and libc-backed
utilities provide the foundation for subsequent application ports.

## References for that discussion

- [GNU ar and archive symbol indexes](https://www.sourceware.org/binutils/docs/binutils.html)
- [Newlib C library and platform hooks](https://sourceware.org/newlib/libc.html)
- [Kilo source](https://github.com/antirez/kilo/blob/master/kilo.c)
- [Neovim build dependencies](https://neovim.io/doc/build/)
- [Lua embedding and standard libraries](https://www.lua.org/manual/5.4/manual.html)
