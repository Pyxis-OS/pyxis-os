# First shell, filesystem and C runtime

Status: agreed milestone direction, with a proposed sequence of focused PRs.
This is a working handoff document, not a frozen ABI or authorization to implement
all tasks at once. Either collaborator can pick up the next assigned task; mark
it complete and record its handoff in the implementing PR.

Before implementing each task, discuss any unclear requirements or unresolved
design choices with the owner. For example, settle the allocator choice before
implementing malloc/free. Agreement on the milestone does not settle those
remaining choices automatically.

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
not authority. Startup represents this context as a sequence of directory
handles, boundary first and current directory last, with an optional display
path. Task 5 supplies the initial chain and helpers that retain an independent
working-directory context in caller-provided storage.

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
Named resources replace the example-specific startup fields; in-tree programs
migrate together without retaining an old layout or bumping its version.
Startup data has a 64 KiB total budget including page padding, allocating only
the pages needed and rejecting oversized input explicitly.

Environment entries are case-sensitive string names and values; empty values
are allowed. The launcher supplies a copied snapshot alongside arguments.
Startup metadata and the initial environment are read-only; argv and its strings
occupy separate writable pages. The runtime owns later process-local changes.
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

- [x] **1. Build a shared native userspace library.** Package existing wrappers
  as `libpyxis` and share startup/link rules across current programs. Establish
  static-library build dependencies without creating unused library skeletons.
  Done when existing images build and behave as before using the shared library.

- [x] **2. Named startup resources, arguments and environment.** Replace fixed
  example roles with bounded, validated startup data for named grants, scheme
  roots, working-directory context, argument strings and environment entries.
  Add native accessors and the common C entry path to `main(argc, argv)`; migrate
  current programs together. Boot launch supplies concrete records, without
  needing userspace launch yet. Define ownership and size limits here.
  Depends on 1. Done when existing programs consume their named resources and
  arguments/environment are inspectable through the runtime.

- [x] **3. Initrd file objects and explicit-offset reads.** Expose immutable
  archive content through the file protocol with READ and size operations,
  using a real packaged file and initial grant. Retain the backing safely and
  handle EOF, partial reads and invalid buffers. Decide the relationship to the
  existing blob implementation without keeping redundant interfaces merely for
  compatibility. Depends on 2. Done when a program reads a supplied file handle.

- [x] **4. Initrd directories, lookup and enumeration.** Expose the archive as
  a read-only directory tree, grant an application root and implement attenuated
  child lookup plus enumeration. Specify iteration under mutation for the later
  RAM backing; no directory snapshots are required by this document. Handle
  capability-table growth and failure without publishing lost handles or leaking
  references. Depends on 3. Done when a program discovers and reads a named file,
  and lists its containing directory through native calls.

- [x] **5. Native path resolution and working-directory helpers.** Resolve
  schemes and relative paths, retain the required directory context and support
  changing that context. Define path syntax and component edge cases explicitly;
  preserve subtree confinement. Depends on 2 and 4. Done when supplied roots and
  relative paths reach the same files without ambient namespace authority.

- [x] **6. Writable RAM directories and file creation.** Supply the shared
  `home://` root, implement CREATE for directories and empty files, and reuse
  lookup/enumeration contracts. Resolve concurrent mutation and BSP allocation
  service needs within existing ownership rules. No disk or overlay work.
  Depends on 4. Done when created entries are discoverable through another
  authorized handle and read-only backing rejects mutation.

- [x] **7. RAM file writes and resizing.** Add explicit-offset writes and resize,
  define writes beyond EOF and contents of grown ranges, and handle partial
  progress and allocation failure. Serialize file mutation without holding locks
  across sleeping BSP allocation requests. Depends on 6. Done when userspace can
  write, reread and resize a RAM file through native operations.

- [x] **8. Private userspace memory backing.** Define the minimal native authority
  and operations a process needs to acquire and release private backing memory.
  Service VM changes on the BSP with explicit address-space ownership while the
  caller is blocked. Do not introduce shared memory objects or a general VM
  redesign. Depends on 2. Done when a real userspace consumer obtains, uses and
  releases backing, and process exit reclaims any remaining regions.

- [x] **9. Allocation and foundational libc.** Build malloc/free and required
  allocation helpers over a suitable userspace allocator, recording dependency
  and license choices if reused. Add the C memory/string, formatting and
  environment functions actually needed by subsequent tasks. Keep runtime
  initialization usable before heap setup. Depends on 1, 2 and 8. Done when
  ordinary userspace consumers use the target libc and normal exit releases
  resources even when applications leave allocations live.

- [x] **10. Process completion objects.** Add an independently retained completion
  result and a wait operation using the existing blocked-task machinery. Connect
  ordinary exit and fatal user faults to it; initially grant observation of a
  boot-launched program to another program. Depends on 2. Done when waiting before
  or after completion returns the same result and closing the observer does not
  stop the observed program or retain its execution memory.

- [x] **11. Userspace launch through a launcher capability.** Accept a readable
  image handle, startup data and attenuated grants; return a process-control
  handle only after complete preparation. Service preparation on the BSP while
  preserving caller table and buffer lifetimes; adapt the loader's input only
  as needed for file-backed images, including a defined policy for images being
  modified during loading. Reuse the path for boot setup where useful.
  Depends on 3 and 10, plus the startup contract from 2. Done when a userspace
  parent launches a child in its space, waits and closes its control handle,
  with failure leaving no runnable partial child or lost parent resources.

- [x] **12. Terminal text input and dimensions.** Extend the current console
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

Task 1 is complete (assistant). The existing wrappers now build into the indexed
static archive `build/userspace/libpyxis.a`, also available through
`make -C userspace libpyxis`. All three programs use the shared link rule with
an explicit startup object and the archive after program code. Native interfaces,
startup behavior and program sources are unchanged. No additional libraries or
runtime facilities were introduced.

Validation: the library-only target and an ordinary parallel image build passed
without warnings. Normal one- and four-CPU KVM boots ran hello, client and server
through exit status 0 and address-space release. Single-CPU framebuffer
inspection confirmed the greeting, archive content, client result and server
closure message. Archive/symbol inspection confirmed an index and selection of
referenced wrappers; hello no longer includes unused endpoint code. A repeated
library build was up to date, and make dry runs showed header and startup changes
triggering their dependent rebuilds. No tests or boot automation were added.

Task 2 is complete (assistant). Startup preparation now lives in `kernel/user/`
and copies named resources, arguments and environment into a process-owned region
bounded at 64 KiB including page padding. Metadata/environment are read-only;
argv and argument strings occupy separate writable pages. Both parts are NX.
Preparation validates inputs and installed handles, publishes only on success,
and releases partial allocations on failure without changing handle ownership.

Libpyxis initializes native accessors before `main(argc, argv)` and exits with
its result, without a userspace allocator. Resource lookup borrows an existing
handle; environment lookup distinguishes missing from empty values. All three
programs migrated together with version 1 retained. Hello uses its argument and
environment in its greeting. Scheme-root and working-directory fields/accessors
exist, but preparation rejects nonempty directory context until directory
objects and their type validation arrive. No path traversal is implemented.

Validation: the ordinary image build completed without warnings; normal one-
and four-CPU KVM boots ran all three programs through exit 0. Framebuffer
inspection confirmed hello's argument/environment output and the existing blob
and endpoint exchange. Manual four-CPU TCG/GDB inspection observed main receiving
argc, its argument string and a final NULL pointer, plus empty directory context.
Kernel calls rejected duplicate resource/environment names, invalid handles,
invalid environment names and repeated preparation. Zero arguments with an empty
environment value were accepted. An exact 64 KiB region succeeded and its final
string terminator survived the multi-page copy; one extra argument byte failed
without publishing a startup address. Page queries confirmed read-only metadata,
writable arguments and NX on both. After temporary inspection allocations and
all programs were released, heap usage and PMM frame counts returned to their
pre-launch values. Allocation-failure unwinding was code-reviewed, not forced.
No tests, fault injection or boot automation were added.

Task 3 is complete (assistant). The immutable blob object and protocol were
replaced outright by the file object, FILE_RIGHT_READ, size and explicit-offset
read operations. `file_create_initrd()` creates the initial read-only backing;
no seek position, directory lookup, mutation or backing-dispatch framework was
introduced. The archive retains its bytes and mapping for the kernel lifetime;
last-reference retirement frees only the file wrapper on the BSP.

Hello and the endpoint example now use file capabilities and libpyxis file
wrappers. The client copies a READ file grant to the server, which reads the
content and closes its handle before replying. Old blob headers, implementations
and library members are gone; protocol values remain unchanged with no version
bump or compatibility layer.

Validation: ordinary image builds completed without warnings. Normal one- and
four-CPU KVM boots ran all three programs through exit 0 and address-space
release. Single-CPU framebuffer inspection confirmed the packaged file contents,
client byte-count response and endpoint closure. Archive/symbol inspection found
the file implementation and no remaining blob symbols. The retained EOF,
short-read, buffer-validation and reference-lifetime paths were code-reviewed;
error paths were not forced in this pass. No tests or boot automation were added.

Task 4 is complete (assistant). The BSP imports the initrd into an immutable
reference-owned directory tree before launch, inferring parents and accepting
explicit empty directories. Files borrow the archive mapping. Typed component
lookup returns an owned child handle with rights bounded by the parent;
enumeration requires separate authority and grants no handles. Native wrappers
need no allocator. Hello receives the `app` scheme root, lists it and `share`,
then discovers and reads `share/hello.txt`. The endpoint example keeps its direct
file grant. Startup validates scheme-root directory types; working-directory
context remains pending task 5. No schema version changed.

Enumeration returns one name/kind and an opaque cursor. A short name buffer
reports required size without advancing; end is repeatable. A mismatched
generation reports CHANGED and requires an explicit restart. The immutable
backing never changes generation; task 6 must synchronize mutation and advance
generation without reuse. Lookup uses the existing BSP table-growth request;
its immutable parent keeps the child alive across the wait. Mutable backing
must revisit this invariant. See [directories](../directories.md) for the contract.

Validation: ordinary image builds passed without warnings. Normal one- and
four-CPU KVM boots ran all three programs through exit 0 and address-space
release; single-CPU framebuffer inspection showed both directory listings and
the discovered file contents. Four-CPU TCG/GDB inspection observed short-buffer
retry, repeatable end, mismatched-generation handling, invalid cursors, output
buffer checks, rights attenuation, wrong-kind and missing-name errors. Filling
hello's table through valid lookups made its normal AP lookup block while the
BSP grew the table from eight to sixteen slots, preserving earlier grants.
A retained child survived destruction of a separately constructed parent tree.
After programs and debugger-owned objects were released, heap/frame counts
returned to their pre-launch baseline and the retirement queue was empty.
Archive conflict handling and allocation-failure unwinding were code-reviewed,
not forced. No tests, fault injection or boot automation were added.

Task 5 is complete (assistant). Libpyxis resolves explicit scheme paths and
relative paths through directory calls. It accepts repeated separators and `.`,
walks `..` through retained ancestors and rejects boundary escape, empty paths
and leading `/`. Trailing `/` requires a directory. Names remain literal,
case-sensitive bytes; no URL decoding, expansion or implicit application search.
Components are walked in order, so `missing/..` cannot bypass a failed lookup.

An explicit context owns directory handles in caller-provided storage. Scratch
arrays and a component buffer bound each operation without imposing ABI-wide
path/depth limits. Directory changes prepare the new chain before replacing the
old one; failures unwind scratch references and preserve the current directory.
Local handle copying supports preserved or reduced rights, uses existing BSP
table growth, and makes every resolved result independently owned, including
`.` and a bare scheme root. Startup now validates/copies directory chains and an
optional descriptive path, without inferring ancestry or changing schema version.
The runtime does not maintain a displayed path or mutate environment variables.
See [paths](../paths.md) for ownership and errors.

Hello starts at the application root, reads `app://share/hello.txt`, changes into
`app://share`, and reads `hello.txt` relatively. It uses no allocator or libc.
The endpoint example still uses direct grants. Bare-command resolution and
executable launching remain later tasks.

Validation: ordinary image builds passed without warnings. Normal one- and
four-CPU KVM boots ran all three programs through exit 0 and address-space
release. Framebuffer inspection showed the explicit and relative reads. Manual
four-CPU TCG/GDB inspection covered parent navigation, boundary rejection,
missing components, trailing-slash kind checks, unknown schemes, small caller
buffers, preserved context after failure, exact result rights and reading a
known filename without ENUMERATE. An independently retained subtree survived
closing its source handle and could not navigate above its boundary. Startup
rejected a non-directory chain and a display path without a chain. COPY rejected
bad buffers/flags/handles and excess rights. Filling the table with valid copies
made an AP's normal copy wait for BSP growth from eight to sixteen entries.
After exit, including debugger-owned handles, physical-frame usage returned to
its pre-launch baseline; heap usage retained only the existing kernel state and
initrd tree, with an empty retirement queue. Allocation-failure unwinding was
reviewed, not forced. No tests, fault injection or boot automation were added.

Task 6 is complete (assistant). One kernel-retained RAM tree supplies `home`
through explicit startup grants to all three boot programs. CREATE accepts one
name, kind and requested rights, returning an owned handle to a new directory or
empty RAM file. CREATE is independent of LOOKUP/ENUMERATE; child grants remain
bounded by the parent's directory or file-grant rights. Existing names return
ALREADY_EXISTS without opening or replacing them. Initrd rejects mutation even
if a grant carries CREATE. File writes, resizing, removal and rename remain out
of scope; new files have size zero and return EOF through the file protocol.

Per-directory locks protect links, counts and generations. Lookup retains the
selected child under the lock before any table-growth wait. Enumeration captures
one result under the lock, then copies its immutable name; the append-only tree
and caller's directory reference preserve that name's lifetime. Successful
creation advances the generation; an old cursor, including END, reports CHANGED.
Generation never wraps. Removal must revisit name lifetime before reclaiming
entries.

A focused BSP service allocates and discards unpublished entries through requests
in task metadata. The requester fills the name from its own validated mappings
and prepares the returned handle before publishing under the directory lock.
Publication rechecks name conflicts. Failure closes any provisional handle and
returns the unused entry for BSP disposal. No directory lock spans allocation or
waiting, and neither heap allocation nor reclamation runs on an AP. Hello creates
`home://notes/empty.txt`, rediscovers both names through independent grants, lists
them and reads the empty file. The tree retains entries after process exit.

Validation: ordinary image builds passed without warnings. Normal one- and
four-CPU KVM boots ran all three programs through exit 0 and address-space
release; framebuffer inspection showed both RAM directory listings. Manual
four-CPU TCG/GDB inspection observed an old END cursor becoming CHANGED after
creation, duplicate-name rejection regardless of kind without advancing the
generation, read-only backing and authority checks, invalid-buffer/name rejection,
and zero-byte reads leaving the data buffer untouched. Entry allocation and
disposal both ran through the BSP service. Filling the caller's table with valid
grants made CREATE wait for growth from sixteen to thirty-two slots while both
directory locks were clear. After exit, physical-frame usage returned to the
pre-launch baseline; heap state retained only existing kernel objects and the
two filesystem trees. Their references matched tree ownership, and request,
completion and retirement queues were empty. Concurrent-creator conflict handling
and allocation-failure unwinding were reviewed, not forced. No tests, fault
injection or boot automation were added; no schema version changed.

Task 7 is complete (assistant). RAM files support explicit-offset WRITE and
RESIZE with independent WRITE authority; SIZE accepts READ or WRITE. Directory
WRITE_FILES controls grants, including through native path resolution. Initrd
backing remains immutable. Libpyxis preserves native file error statuses.

Writes and resize are all-or-nothing. Gaps and grown ranges read as zero,
truncated bytes cannot reappear, and zero-byte writes do not extend the file.
Contiguous buffers grow geometrically with an exact-size fallback. Nonzero
shrinks retain capacity; resize to zero releases it. The storage tradeoff is
recorded in `docs/technical-debt.md`.

Per-file FIFO operation ownership serializes readers and writers across BSP
backing requests without a held spinlock. Request/wait records live in task
metadata; user copies remain on the caller CPU. The normal hello consumer
writes and truncates through a WRITE-only handle, then rereads through an
independently looked-up READ grant. The home tree retains those contents after
process exit.

Validation: ordinary image builds completed without warnings. Normal one- and
four-CPU KVM boots ran all three programs through exit 0 and address-space
release; framebuffer inspection showed the RAM-file greeting after truncation.
Manual four-CPU TCG/GDB inspection confirmed gap zeroing, shrink/regrowth within
retained capacity, growth from empty, release on resize to zero, write-only SIZE,
rights/buffer/overflow rejection and immutable initrd backing. A resize beyond
the allocator's supported size returned NO_MEMORY with old contents, size and
capacity intact. BSP replacement ran with the spinlock clear. GDB-held operation
ownership let file calls from two CPUs queue and sleep; release handed ownership
to each in FIFO order and normal program execution completed afterward.

After process exit, physical-frame usage returned to its pre-launch baseline;
request and retirement queues were empty. The home tree retained its file data
with one parent-owned reference, no active operation and no waiters. Physical
memory exhaustion and the exact-size allocation fallback were code-reviewed,
not forced. No tests, fault injection or boot automation were added, and no
schema version changed.

Task 8 is complete (assistant). A named memory-service grant authorizes ALLOCATE
and RELEASE through one MANAGE right. Authority applies to the calling process,
even after handle transfer; the stateless service retains no process pointer.
Allocation returns eager zeroed RW/NX backing at a kernel-chosen address, rounded
to pages. Release requires one exact current service allocation and cannot free
image, startup or initial stack mappings. Regions survive handle close and are
reclaimed on process exit. No per-region handles or userspace allocator yet.

The caller stages its request in task metadata and sleeps. Its scheduler leaves
the private root and task stack before publishing the request and lending VM
ownership to BSP. The BSP mutates the inactive space without a queue lock, then
returns ownership on wake; resumption reloads CR3 before task access. The process
owns a dynamically allocated record list, separate from the existing VM ranges.
BSP process destruction reclaims backing through VM and then frees the records.
See [private memory](../memory.md) for buffer lifetimes and errors.

Libpyxis supplies allocation/release wrappers. Hello uses acquired backing for
file-read buffers and releases it after each file, while its path workspace
remains until exit. All boot programs receive explicit memory grants. Startup
layouts and versions are unchanged.

Validation: ordinary image builds passed without warnings. Normal one- and
four-CPU KVM boots ran hello, client and server through exit 0 and address-space
release; the single-CPU framebuffer retained the expected file output. Manual
four-CPU TCG/GDB inspection observed an AP requester parked, its CPU on the kernel
root and the queue lock clear before BSP allocation. A 4,097-byte request returned
8,192 zeroed bytes with user/writable/NX permissions; resumption restored the
private root.

GDB calls rejected missing authority, malformed messages, zero/overflowing sizes,
read-only replies, partial/repeated release and image/stack/startup release.
Virtual-range exhaustion returned NO_MEMORY without a published allocation.
RELEASE accepted a request residing in the released region and did not touch
that user memory afterward. Two programs held the same service object: a call
from the server could not release a region belonging to hello, and the server
could allocate/use/release its own region through its actual capability.

Hello's final workspace remained mapped after closing its memory handle. Exit
freed its backing and allocation record; frame usage returned to the pre-launch
baseline and memory/completion/retirement queues were empty. Physical-memory and
metadata exhaustion unwinding were code-reviewed, not forced. No tests, fault
injection or boot automation were added; no schema version changed.

Task 9 is complete (assistant). The userspace library separately compiles the
existing pinned BSD-3-Clause TLSF allocator. Each process retains a private
memory-service handle and obtains pools lazily, with a 64 KiB minimum and no
fixed pool-count limit. malloc/calloc/realloc preserve 16-byte alignment, check
size arithmetic and report exhaustion through errno. Free blocks are reusable;
pools remain mapped until exit. Failed realloc preserves the old block, and
shrinking retains capacity. These tradeoffs are in `docs/technical-debt.md`.

Libc owns C entry/exit and builds as a separate target archive. Native startup
validation and binding initialization remain allocation-free; libpyxis retains
native operations and their status results. The initial C subset provides memory
operations, string lengths/comparisons/search/duplication, snprintf/vsnprintf,
getenv over immutable startup values, and process-local errno. Formatting supports
integer/string conversions, width, precision and integer length modifiers;
unsupported formats fail explicitly. No streams, floating-point formatting,
environment mutation or exit handlers yet. Runtime contracts live beside the
headers and in `docs/userspace.md`.

Hello now allocates file buffers through libc, frees them after use, formats its
greeting and reads environment through getenv. Its path workspace remains live
until exit. Client uses bounded integer formatting for the service reply count.
The kernel ABI and schema versions are unchanged.

Validation: ordinary image builds completed without warnings; one- and four-CPU
KVM boots ran all three programs through exit 0 and address-space release.
Framebuffer inspection showed file output and the formatted client byte count.
Manual four-CPU TCG/GDB inspection confirmed aligned pool growth, calloc zeroing,
moving realloc content preservation, overflow rejection with the old allocation
intact, string duplication/search, overlapping memmove, environment lookup, and
formatting truncation, integer extrema, width/precision and count overflow.
Unsupported formatting returned EINVAL.

Allocation still worked after hello closed its original memory grant. Exit
reclaimed four retained pools, including a live allocation, and physical-frame
usage returned to its pre-launch baseline. Request/completion queues and object
retirement were empty. Physical-memory exhaustion and missing-grant handling were
reviewed, not forced. No tests, fault injection or boot automation were added.

Task 10 is complete (assistant). A process-control object retains immutable
completion separately from process execution state. Its single WAIT right
authorizes a header-only tagged request. The result distinguishes EXITED with a
signed 32-bit exit status from FAULTED; fault details remain in the kernel log.
Libpyxis provides process_wait with native status results and reply validation.

Observers may wait before or after completion, and repeated/concurrent waits
receive the same result. Closing an observer does not stop execution. The BSP
reaper takes the execution owner's control reference, reclaims the process,
private mappings, capability table, task stack and metadata, then publishes and
wakes observers. Capability object destruction follows normal deferred retirement.
The retained control object has no process pointer. Wait records live in task
metadata, with completion-to-scheduler lock ordering and wake-before-sleep
handling. No remote task-stack pointers or allocation on the wait path.

Client receives a named server_process grant. It closes its endpoint after the
exchange, allowing server to exit, then waits, reports the result and closes the
observer. There is no userspace launch or termination operation yet. See
`docs/processes.md` for the completion contract; ABI/schema versions are unchanged.

Validation: ordinary image builds passed without warnings. Normal one- and
four-CPU KVM boots completed all three programs with status 0; framebuffer
inspection showed client reporting server's completion after its cleanup log.
Manual four-CPU TCG/GDB inspection observed two tasks parked on the same control
object, publication from the BSP reaper after execution cleanup, both waiters
being detached/woken, and the same result from a later wait. Invalid authority,
request shape/operation, reply storage and stale handles were rejected. Closing
the last observer before a program started did not prevent its normal execution.
Closing the final retained observer after completion retired the control object.
Physical-frame usage returned to the pre-launch baseline; request/completion and
object-retirement queues were empty. Fatal-user-fault routing, nonzero status
propagation and allocation-failure unwinding were code-reviewed, not forced.
No tests, fault injection or boot automation were added.

Task 11 is complete (assistant). A stateless launcher capability authorizes a
child in the caller's space on its assigned CPU. There is no target-space/CPU
parameter. LAUNCH accepts a readable P1F image file, explicit source-handle/right
grants, resources, roots, directory context, environment and argv. Bindings refer
to grant indices, preserving intentional aliases. All rights are equal or reduced;
there is no implicit inheritance, including memory or launcher authority.

Caller-side capture checks nested pointers/counts against a combined 64 KiB
array/string/alignment budget. The child startup region separately obeys its
existing 64 KiB padded limit. BSP allocates/disposes staging, prepares a private
process/stack, copies grants and startup, and installs the parent's WAIT handle
before submitting the child. Failures unwind unpublished resources and preserve
source handles. The caller lends its table while blocked; BSP never reads caller
virtual pointers or remote stack storage. Boot and runtime launch share image
and stack preparation. No schema/version changes.

The file's existing operation ownership keeps executable bytes stable through
loading. Reads/writes/resizes queue without holding a spinlock; ownership is
released before submission. No additional whole-image copy or generic reader
abstraction. Staging and synchronous preparation costs are recorded in
`docs/technical-debt.md`; the launch contract is in `docs/processes.md`.

Boot now starts hello and client. Client receives the server image, a launcher
and both endpoint ends; it launches server with explicit output/endpoint/memory
grants and argv, drops preparation-only handles, performs the exchange and waits
for completion. Server runs on client's CPU and does not inherit home or environment.
Libpyxis exposes an allocation-free launcher_launch wrapper with native statuses.

Validation: ordinary image builds passed without warnings. One- and four-CPU
KVM boots completed hello, client and the userspace-launched server with status 0;
framebuffer inspection confirmed the exchange and completion report. Manual
four-CPU TCG/GDB calls rejected invalid authority, request shape, image handles/
types/contents, nested pointers/counts, excess rights, duplicate names and invalid
grant indices. Failed preparation released file ownership and preserved sources.
A valid launch with aliased resources, an attenuated directory grant, root,
working-directory context and environment delivered the expected child startup.
Filling the parent's table made it grow before submission to accommodate the
observer. Child space/CPU matched its parent, file/queue spinlocks were clear
during preparation, and the image operation was released before task submission.
After exit, physical-frame use returned to baseline and launch, request,
completion and retirement queues were empty. Mutable-image writer contention,
physical allocation failure and late task-allocation failure were reviewed,
not forced. No tests, fault injection or boot automation were added.

Task 12 is complete (assistant). Console READ and SIZE extend the existing tagged
protocol without a version bump. Hello receives a separate READ-only input grant,
queries dimensions, waits for one input chunk and echoes it before exiting.
READ sleeps without echo/editing, permits short reads, and succeeds immediately
for zero capacity. SIZE accepts READ or WRITE and reports character cells below
the tab bar. Libpyxis preserves native statuses for both operations.

Each space console owns a 4 KiB queue. The BSP session task consumes shortcuts
before routing US ASCII text and navigation sequences to the active application
space. Caelum discards text except in the single-CPU fallback. Shift, Caps Lock,
repeat and Ctrl+letters are supported; navigation includes arrows, Home/End,
Delete and Page Up/Down. Whole sequences enter atomically but can span reads.
Overflow/device loss clears queued input and latches INPUT_LOST until a nonempty
read acknowledges it. Device loss invalidates every application queue because a
space shortcut may have been lost. Unavailable hardware returns UNAVAILABLE.
Readers acquire ownership FIFO and sleep through the existing task wait mechanism.
Cursor editing and history remain userspace work; see `docs/keyboard.md` and
`docs/processes.md` for the contract.

Validation: ordinary image builds passed without warnings. Normal one- and
four-CPU KVM boots ran the existing programs; hello waited for input, echoed a
key and exited with status 0. Four-CPU inspection confirmed Caelum discards text,
Alt+Right selects the application space, and Shift+A wakes its reader. Dimensions
matched the TTY area. Manual TCG/GDB inspection observed a parked reader with
input/scheduler locks clear, wakeup and detached wait links, expected navigation
sequences and Caps Lock/Shift/Control bytes. Zero-length reads succeeded; invalid
rights, request sizes, operation tags and buffers were rejected. Frame usage
returned to the pre-launch baseline after exit. Multiple-reader ordering,
overflow/device-loss recovery and unavailable hardware were code-reviewed, not
forced. No tests, fault injection or boot automation were added.

Task 13 remains unstarted. Discuss line-input limits, control handling, Escape
prefix decoding and editing scope before implementing libterm.
