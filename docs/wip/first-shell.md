# First shell, filesystem and C runtime

Status: agreed milestone direction, with a proposed sequence of focused PRs.
This is a working handoff document, not a frozen ABI or authorization to implement
all tasks at once. Either collaborator can pick up the next assigned task; mark
it complete and record its handoff in the implementing PR.

This narrows the candidates in [development paths](development-paths.md).
The [filesystem draft](../vfs.md) describes the longer-term overlay and namespace
model; [processes](../processes.md) and [endpoints](../endpoints.md) describe the
current implementation. Decisions below describe the target of this milestone,
not functionality that already exists.

## Milestone and boundary

Boot into an interactive shell in an application space. The shell can navigate
directories, launch a program with arguments and explicit resources, wait for
its completion, and return to the prompt after an ordinary exit or user fault.
Separate `ls`, `cat` and `mkdir` programs exercise a small C library and native
filesystem interfaces. Programs can also create, write and reread RAM files.

The first namespace has:

- `app://`: an initrd-backed, read-only application tree.
- `home://`: a shared writable RAM tree, with contents lost on reboot.

The shell uses `libpyxis` for native operations and `libterm` for interaction.
Utilities use an intentionally incomplete libc, with native calls where ISO C
has no suitable interface. The shell shares the same C startup and foundational
libc; it does not need a separate runtime.

Keep one task per process, pinned to its space's CPU. Keep existing BSP allocation,
VM mutation and reclamation restrictions explicit when servicing AP callers.
This milestone does not require a new scheduling or memory-management model.

Out of scope: disk persistence, overlays and publication, filesystem servers,
links, mount management, POSIX compatibility, background jobs, pipes, redirection,
shell scripting or expansion, job control, process cancellation, editors, Lua,
networking, custom object/library formats and a new toolchain target. General
endpoint reply attachments and capability moves are not prerequisites for
kernel directory operations returning newly installed handles.

## Filesystem contract

The VFS lives in the kernel. Directory and file objects use the existing tagged
synchronous call model and process-local capabilities. Keep backing-specific
code focused; do not introduce a generic plugin or driver framework for two
initial backings.

A directory capability refers to a logical directory in the caller's granted
view, rather than an overlay layer. Initially that view is one tree. Lookup
returns a capability to an object. Removing or replacing a name affects future
lookups; existing open capabilities keep their object alive. File capabilities
have no shared implicit seek position: reads and writes carry explicit offsets.

| Object | Right | Authorized operations |
| --- | --- | --- |
| File | `READ` | Read contents and query size |
| File | `WRITE` | Write, resize and query size |
| Directory | `LOOKUP` | Resolve a child name |
| Directory | `ENUMERATE` | List entries |
| Directory | `CREATE` | Create files and directories |
| Directory | `REMOVE` | Remove entries |
| Directory | `READ_FILES` | Grant file READ through lookup |
| Directory | `WRITE_FILES` | Grant file WRITE through lookup |

Lookup requests the rights needed by the caller. File grants are bounded by the
parent's file-grant rights; subdirectory grants are subsets of the parent
capability's directory rights. These rights have meaning within their respective
object protocols. Backing storage can still reject mutation, notably on initrd.

The application root grants lookup, enumeration and file reading. Home can also
grant file writing, creation and removal. Writing a file does not grant removal
or rename of its directory entry. Rename needs REMOVE on the source directory
and CREATE on the destination; replacing a destination also needs REMOVE there.
Remove and rename semantics are recorded here for later work; neither operation
is required by the initial utility set.

Kernel child lookup accepts one ordinary name, excluding `/`, `.` and `..`.
Directory enumeration does not implicitly grant capabilities to listed objects.
No global namespace lookup or Unix permission-bit model is introduced.

### Paths and working directories

Startup supplies named scheme roots as directory capabilities. `libpyxis`
resolves paths by selecting a supplied root and walking components with only
the necessary rights. Unknown or ungranted schemes fail; there is no fallback
to a global root. Reading a known filename need not require ENUMERATE.

Bare command names resolve under `app://`; an explicit URI selects its scheme,
and `./program` selects the working directory. File arguments such as
`notes.txt` are relative to the application's working directory.

The working directory is userspace runtime state, passed explicitly to children.
It includes directory capabilities, so a rename does not silently retarget
relative access. A child changing its working directory does not change its
parent's. Parent navigation uses retained or granted ancestor capabilities;
`..` must not escape a standalone subtree grant. A displayed path is descriptive,
not authority. The exact startup representation is left to task 2.

## Launch and startup contract

A launcher capability authorizes creation of a process in the caller's space,
on the same assigned CPU. The caller supplies a readable executable-image
capability, arguments, environment, explicitly named initial grants and working-
directory context. No separate file EXECUTE right is needed initially: launcher
authority controls process creation. There is no automatic capability-table
inheritance; all child grants have equal or reduced source rights.

Prepare the image, stack, startup data and grants before making the child
runnable. Failure unwinds unpublished child resources and leaves parent handles
unchanged. A successful launch returns a process-control capability whose first
operation is waiting for completion. Completion distinguishes exit status from
a fatal userspace fault. It remains observable while a control handle exists,
without retaining the terminated process's address space. Closing that handle
does not terminate the child.

C startup consumes the native startup record, initializes runtime bindings,
calls `main(argc, argv)`, and exits with its return value. Programs discover
resources by their startup names through `libpyxis`, not fixed handle numbers.
Replace the current example-specific startup roles and migrate in-tree programs
together. Do not retain an old layout or bump its version just for this change.

Environment entries are case-sensitive string names and values; empty values
are allowed. The launcher supplies a copied snapshot alongside arguments.
Initial startup data is read-only; the runtime owns later process-local changes.
The shell normally copies its environment to children, with explicit overrides.
There is no kernel-global environment or implicit inheritance.

Values such as `EDITOR=app://kilo`, `HOME=home://` or `PWD` express preferences or
descriptions, never authority. Directory capabilities remain authoritative.
No PATH search is needed initially. Lua may eventually generate this environment,
but applications will not need Lua to consume it.

## Terminal and library contract

A terminal capability permits input, output or both. Input is blocking byte
reads with partial results; output is byte writes. Applications can query size
in character cells. Keyboard layout translation sits above the physical driver.
Text input and physical key events remain distinct interfaces; this milestone
needs the text interface only. Global space-navigation shortcuts remain owned
by the session and are not delivered as application input.

The shell reads commands, passes its terminal to a foreground child, stops
reading while waiting, and resumes on completion. This relies on cooperating
applications; foreground ownership arbitration and background execution are
later work. Kernel terminal input does not perform line editing or echo.

`libterm` provides explicit-handle operations for input, output, write-all,
dimensions and focused cursor/clear/style helpers for supported terminal
sequences. Its line-input helper performs editing and echo in userspace.
Initially support printable text, Enter and Backspace with a caller-provided
buffer, an explicit result and returned length. Do not silently truncate a line.
History and cursor-based line editing can follow later. A terminal context
borrows its handle by default; its owner remains responsible for closing it.
There is no implicit global terminal in the native API.

`libpyxis` owns native object-call wrappers, path/resource lookup and launch
interfaces. `libterm` builds terminal behavior on those interfaces. Libc supplies
C startup, allocation, strings, memory functions, formatting, environment access
and the subset of stdio needed by the programs. Share implementations where
appropriate without making the freestanding kernel depend on userspace libc.

For example, `fopen("home://notes.txt", "r")` resolves through granted roots and
holds a file capability. Each `FILE` owns its stream position and buffering;
terminal-backed standard streams use explicitly supplied startup resources.
No kernel file-descriptor table or POSIX open/read syscalls are required.
Directory enumeration and creation are not ISO C facilities: `ls` and `mkdir`
can use native directory calls alongside libc. POSIX-shaped wrappers are a
separate decision, not required here.

Grow libc from concrete uses rather than attempting completeness. Choose a
userspace allocator and its backing-memory contract together; it is independent
of the kernel heap. No host libc or host startup code enters target binaries.
Static libraries can use the existing ELF object/link workflow and indexed `.a`
archives before final PXE conversion.

## PR worklist

The numbering provides stable handoff references. Dependencies describe the
usable prerequisite, not a requirement to do unrelated branches in parallel.
Each task should include its native wrappers and necessary in-tree consumer
updates. Exact message layouts and status names belong beside the implementation,
not duplicated here as a second ABI definition.

- [ ] **1. Build a shared native userspace library.** Package existing wrappers
  as `libpyxis` and share startup/link rules across current programs. Establish
  static-library build dependencies without creating unused library skeletons.
  Done when existing images build and behave as before using the shared library.

- [ ] **2. Named startup resources, arguments and environment.** Replace fixed
  example roles with bounded, validated startup data for named grants, scheme
  roots, working-directory context, argument strings and environment entries.
  Add native accessors and the common C entry path to `main(argc, argv)`; migrate
  current programs together. Boot launch supplies concrete records, without
  needing userspace launch yet. Define ownership and size limits here.
  Depends on 1. Done when existing programs consume their named resources and
  arguments/environment are inspectable through the runtime.

- [ ] **3. Initrd file objects and explicit-offset reads.** Expose immutable
  archive content through the file protocol with READ and size operations,
  using a real packaged file and initial grant. Retain the backing safely and
  handle EOF, partial reads and invalid buffers. Decide the relationship to the
  existing blob implementation without keeping redundant interfaces merely for
  compatibility. Depends on 2. Done when a program reads a supplied file handle.

- [ ] **4. Initrd directories, lookup and enumeration.** Expose the archive as
  a read-only directory tree, grant an application root and implement attenuated
  child lookup plus enumeration. Specify iteration under mutation for the later
  RAM backing; no directory snapshots are required by this document. Handle
  capability-table growth and failure without publishing lost handles or leaking
  references. Depends on 3. Done when a program discovers and reads a named file,
  and lists its containing directory through native calls.

- [ ] **5. Native path resolution and working-directory helpers.** Resolve
  schemes and relative paths, retain the required directory context and support
  changing that context. Define path syntax and component edge cases explicitly;
  preserve subtree confinement. Depends on 2 and 4. Done when supplied roots and
  relative paths reach the same files without ambient namespace authority.

- [ ] **6. Writable RAM directories and file creation.** Supply the shared
  `home://` root, implement CREATE for directories and empty files, and reuse
  lookup/enumeration contracts. Resolve concurrent mutation and BSP allocation
  service needs within existing ownership rules. No disk or overlay work.
  Depends on 4. Done when created entries are discoverable through another
  authorized handle and read-only backing rejects mutation.

- [ ] **7. RAM file writes and resizing.** Add explicit-offset writes and resize,
  define writes beyond EOF and contents of grown ranges, and handle partial
  progress and allocation failure. Serialize file mutation without holding locks
  across sleeping BSP allocation requests. Depends on 6. Done when userspace can
  write, reread and resize a RAM file through native operations.

- [ ] **8. Private userspace memory backing.** Define the minimal native authority
  and operations a process needs to acquire and release private backing memory.
  Service VM changes on the BSP with explicit address-space ownership while the
  caller is blocked. Do not introduce shared memory objects or a general VM
  redesign. Depends on 2. Done when a real userspace consumer obtains, uses and
  releases backing, and process exit reclaims any remaining regions.

- [ ] **9. Allocation and foundational libc.** Build malloc/free and required
  allocation helpers over a suitable userspace allocator, recording dependency
  and license choices if reused. Add the C memory/string, formatting and
  environment functions actually needed by subsequent tasks. Keep runtime
  initialization usable before heap setup. Depends on 1, 2 and 8. Done when
  ordinary userspace consumers use the target libc and normal exit releases
  resources even when applications leave allocations live.

- [ ] **10. Process completion objects.** Add an independently retained completion
  result and a wait operation using the existing blocked-task machinery. Connect
  ordinary exit and fatal user faults to it; initially grant observation of a
  boot-launched program to another program. Depends on 2. Done when waiting before
  or after completion returns the same result and closing the observer does not
  stop the observed program or retain its execution memory.

- [ ] **11. Userspace launch through a launcher capability.** Accept a readable
  image handle, startup data and attenuated grants; return a process-control
  handle only after complete preparation. Service preparation on the BSP while
  preserving caller table and buffer lifetimes; adapt the loader's input only
  as needed for file-backed images, including a defined policy for images being
  modified during loading. Reuse the path for boot setup where useful.
  Depends on 3 and 10, plus the startup contract from 2. Done when a userspace
  parent launches a child in its space, waits and closes its control handle,
  with failure leaving no runnable partial child or lost parent resources.

- [ ] **12. Terminal text input and dimensions.** Extend the current console
  object into the needed terminal contract, with input rights, blocking reads,
  size queries and per-space text queues. Route keyboard input to the active
  application space after session shortcuts; select and document the first
  supported layout. Define queue overflow, wakeup and unavailable-input behavior.
  Depends on 2. Done when a userspace reader sleeps awaiting text, receives it
  in the selected space, and global navigation still works.

- [ ] **13. Shared libterm and line input.** Implement the explicit-handle API,
  supported terminal controls and the small line-input helper. Settle buffer-full,
  end-of-input, Backspace and supported text-encoding behavior here; do not imply
  complete Unicode editing or display-width support. Depends on 1 and 12.
  Done when an interactive userspace consumer reads and echoes edited lines,
  queries dimensions and handles errors without silently accepting a short line.

- [ ] **14. File and terminal stdio.** Add `FILE`, open/close, read/write, required
  formatting, EOF/error reporting and flushing. Define the supported fopen modes
  and initial buffering policy. Bind standard streams from startup grants and
  flush owned output on normal C exit. Keep native errors available below libc;
  define their C-facing translation. Depends on 5, 7, 9 and 13. Done when a C
  program writes and rereads a RAM file and prints it using libc facilities.

- [ ] **15. cat using libc.** Build and package a separate program using stdio
  to copy named file contents to stdout, handling partial I/O and errors with a
  meaningful exit status. Depends on 14. Done when it displays both an initrd
  file and a RAM file using arguments from startup.

- [ ] **16. ls and mkdir.** Build and package separate programs combining libc
  with native enumeration/creation helpers. Keep options minimal; no POSIX
  compatibility requirement. Depends on 5, 6 and 14. Done when they list the two
  roots and create/list a RAM directory, reporting missing authority and other
  failures through their normal output/status paths.

- [ ] **17. Interactive foreground shell.** Parse commands with arguments and
  quoted strings, implement `cd` and `exit`, resolve bare names under `app://`,
  and launch with explicit resources, environment and working-directory context.
  Define the small quoting syntax without expansion or scripting. Wait without
  reading the terminal, report exit/fault outcomes and resume the prompt.
  Depends on 5, 9, 11 and 13. Done when a user can launch a packaged program,
  change directory and launch another without restarting the shell.

- [ ] **18. Make the shell the normal application-space entry.** Package the
  shell, utilities and sample content, grant the application/home roots, launcher
  and terminal, and replace the boot-only demo sequence for normal use. Define
  what remains visible after the shell exits; no supervisor/restart framework is
  required. Depends on 15, 16 and 17. Done when normal boot supports the complete
  milestone walkthrough and leaves the pinned Caelum log/session UI intact.

## Validation and handoff

Each implementation PR uses ordinary builds, normal QEMU boots and debugger
inspection where useful. Do not add tests, self-tests, fault injection, output
matching or boot automation. Exercise useful behavior through actual userspace
consumers as tasks land. Record what was observed separately from code review.

For the completed milestone, boot normally, switch to the application space,
list `app://`, display a packaged file with `cat`, create and enter a directory
under `home://`, and launch a utility with relative and explicit-scheme paths.
The stdio task's consumer establishes file creation/write/read support without
adding shell redirection. Observe that child completion restores the prompt and
that process memory and handles are reclaimed. Check single- and multicore use
within the existing supported configuration; application faults must leave the
session and kernel log usable.

For each completed task, add a short handoff below: implementing PR, final scope,
validation actually performed and any dependency or contract adjustment needed
by the next task. Keep the checklist aligned with merged work. Implementation
may reveal a better split; update this document rather than silently expanding
a PR or treating every detail here as permanently fixed.

No implementation tasks are complete yet.
