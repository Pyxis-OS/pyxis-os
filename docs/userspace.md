# Userspace

## Building an image

`make userspace` builds the freestanding programs and the hosted `elf2pxe`
converter. Each program has a directory and an explicit target in the
[userspace Makefile](../userspace/Makefile); for example,
`make -C userspace hello` builds just that program. Pass `CROSS_COMPILE` as for
kernel builds and `HOSTCC` for the converter. Outputs live under `build/`.

The native wrappers build into `build/userspace/libpyxis.a`; use
`make -C userspace libpyxis` to build just the library. The target C library builds separately into
`build/userspace/libc.a` (`make -C userspace libc`). Every program links libc
startup explicitly, then its code and the libc, libpyxis and libterm archives;
only referenced archive objects are pulled in. No host runtime is linked.
Libpyxis owns native operations and startup accessors; libc owns C entry/exit,
allocation and the initial C support routines.

To convert an already linked executable:

```sh
build/tools/elf2pxe --format p1f -o hello.pxe hello.elf
```

The input must be a fixed-address, little-endian x86_64 ELF executable with no
interpreter, dynamic linking, runtime relocations or TLS. The
[linker script](../userspace/linker.ld) separates segment permissions on page
boundaries; writable executable mappings are rejected. The
[P1F header](../include/pxe/p1f.h) defines the image layout and constraints.
Keep the ELF for debugging; the converted image does not replace its symbols.
See [gdb.md](gdb.md) for kernel debugger usage.

## Entry and loading

`make initrd` packages hello and its text asset, plus client, server, cat, ls, mkdir and shell
programs, into `build/initrd.cpio`; `make image`
includes that uncompressed `newc` archive as the sole Limine module. The kernel
keeps the archive mapped read-only and uses `initrd_lookup()` to find the program
and content by exact archive names. The text source lives beside hello in
[message.txt](../userspace/hello/message.txt). See [the initrd interface](../include/kernel/initrd.h)
for the supported records and borrowed-view lifetime. The archive reader provides raw entry views. A separate boot-time
[tree builder](../include/kernel/fs/initrd_tree.h) exposes those entries as
read-only directory/file objects without extracting file contents or adding a
block device. The text asset is packaged at `share/hello.txt`.

The image loader creates an inactive private address space, copies the program
into owned backing and applies its permissions. Failure releases partial
allocations; loading does not change the caller's active address space.

The kernel supplies an executable entry point and a writable, 16-byte-aligned
user stack. Entry receives a pointer in `RDI` to the
[startup region](../include/abi/startup.h), which remains mapped until process
exit. Its address is chosen by VM allocation; programs must use the pointer.
The region carries named resources, environment, arguments and optional directory
context, within a 64 KiB budget including page padding. Resource/environment
metadata is read-only; argv and its strings occupy separate writable pages.
Neither part is executable.

The libc entry validates and initializes the native startup snapshot without
heap allocation, prepares the allocator and standard streams, then calls
`main(int argc, char **argv)`,
then exits with main's return value. Use
[startup_resource() and the other native accessors](../userspace/include/startup.h)
to find supplied handles and environment values. Lookup borrows an existing
handle and never duplicates it. Missing resource names return HANDLE_INVALID;
missing environment values return NULL, while an empty value is an empty string.
`getenv` borrows these same immutable values; environment mutation is not
implemented. Hello receives an `app` directory root
and a working-directory chain starting at that root. The
[path context](paths.md) retains its own copies of these handles. Boot-launched hello and client also receive grants to one shared RAM-backed
`home` root and the caller-scoped [private-memory service](memory.md). Client
explicitly passes memory authority when it launches server.

Hello obtains named output and its `app` scheme root, uses its argument vector
and environment in its greeting, then enumerates the root and its `share` child.
It reads the text file first through an explicit scheme path, then relatively
after changing into `share`, closing each owned handle even after an I/O error.
Hello then creates a RAM directory through the native interface and uses
[stdio](stdio.md) to open a file, write formatted text, seek back and copy its
contents to stdout. Its greeting also uses libc output; stdio failures use perror.
After client/server completion, Hello launches `cat` to copy the initrd text and
the RAM file, `mkdir` to create a RAM directory, and `ls` to list both roots and
the new directory. It waits between children and before starting line input.
Each utility receives output, memory and a `home://` working directory. Filesystem
grants differ: cat can read files, mkdir can create under home (its app binding
has no rights), and ls can enumerate both roots. None receives input or launcher
authority.
See [directories.md](directories.md) for lookup, enumeration and exclusive creation.
The syscall register
convention is defined in the [native wrapper](../userspace/include/syscall.h).
Normal output targets the owning space's TTY; diagnostic output targets the
kernel log, which goes to the Caelum TTY and serial.

The [console wrapper](../userspace/include/console.h) uses the native CALL ABI
and reports actual bytes written. Its byte-count and string helpers finish partial writes;
each kernel call renders a bounded chunk. Requests and replies have shared
layouts, and CALL returns both status and reply byte count. All programs use
console capabilities for TTY output. Hello additionally receives a READ-only
`input` handle and uses [libterm](terminal.md) to query dimensions and read an
edited line. A WAIT-only `client_process` grant lets it wait for the other
application writers before drawing its prompt. It retries after Ctrl+C cancellation or input loss, prints an
accepted line and exits. On multicore boots select its CPU tab with Alt+Right
before typing; typing on Caelum is discarded. The single-CPU fallback accepts
input on Caelum, but concurrent kernel logs can disrupt the editor's display.
Missing keyboard input is reported without blocking indefinitely. History and
lines larger than the visible terminal remain later work.
See [keyboard input](keyboard.md) for the layout and navigation bytes.

CALL takes a handle, a tagged message and its size, then a reply buffer and
capacity. Shared protocol headers define the tag and payload union. Rights are
checked against the handle's object type, so the same bit may mean console
WRITE or file READ. Kernel and userspace are rebuilt together against the
shared ABI headers. Older layouts are not supported.

The [file wrappers](../userspace/include/file.h) query size, read/write at explicit
offsets and resize RAM files. They preserve native error statuses and check reply
lengths/counts. Writes complete in full or leave the file unchanged; gaps and
newly grown ranges read as zero. A short read is allowed and zero bytes with
nonzero capacity means EOF. Hello uses `malloc` for a small read buffer and `free` after each file; buffer size does not depend on file
size, and content need not be NUL-terminated. It checks that EOF and transferred counts agree with the queried size.

The [handle wrapper](../userspace/include/handle.h) releases the calling process's
reference. A closed handle is immediately stale; other owners, including the
space that owns the console, retain their references. Process exit releases
handles left open. The read-only startup record is not updated after close.

Client launches server through a granted launcher and a readable image handle.
It explicitly supplies output, endpoint and memory grants, plus the child argv.
No roots or environment are inherited. See [launch](processes.md#implemented-userspace-launch)
for request bounds, source-grant ownership and mutable-image behavior.

The client and server use the [endpoint wrappers](../userspace/include/endpoint.h)
to exchange a structured request with an attached capability. The client receives
a content file at startup and copies a READ grant to the server. The server
reads and prints its contents, closes its received handle, then replies with
the byte count. The client prints the result and closes its own content grant,
which remained valid. Closing its endpoint then lets the server exit. The client uses the launch result
process-control capability to wait for server cleanup and report completion
before closing that observer handle. See [process completion](processes.md#implemented-process-completion)
for the lifetime and repeatable wait contract.
Both have output handles; the server has no startup content grant. Their
endpoint rights are CALL for the client and RECEIVE | REPLY for the server.
The [endpoint contract](endpoints.md) describes ownership, growth and errors.

The [boot launcher](../kernel/user/launch.c) prepares hello and client. Both boot
and userspace launch use [shared image/stack preparation](../kernel/user/load.c)
to create a process that owns the
loaded address space and belongs to the target CPU's space. Before submission, it calls
`process_prepare_startup()` with named bindings to already installed handles,
arguments and environment. Preparation validates and copies the supplied kernel
data into zeroed process-owned pages through a temporary alias, without activating
the process root. It leaves metadata read-only and only arguments writable.
Task submission transfers
sole ownership of this process on success; on failure it remains with the caller
for cleanup. There is initially one user task per process. Each task also owns
a private kernel-entry stack. Allocation, submission and reclamation stay on the BSP,
while tasks can run on their assigned AP. See the
[task interface](../include/kernel/user.h) for the ownership contract and
[smp.md](smp.md) for CPU selection and cross-CPU handoff rules.

## cat

`cat path...` copies one or more named files through libc stdio with a fixed
transfer buffer. It accepts native scheme paths and relative paths when its
caller supplies directory context. There are no options: every argument is a
path, including `-`. With no paths it prints usage to stderr and exits with failure;
implicit stdin is deferred until terminal input has an EOF convention.

Input errors are reported to stderr with the path, and copying continues with
the remaining arguments. Any error makes the exit status unsuccessful; an output
error stops copying immediately. Files are copied as bytes without separators,
text conversion or an added newline.

## ls and mkdir

`ls [path...]` lists directory entries in native enumeration order, one per line,
with `/` after directory names. No arguments lists the supplied working directory;
multiple paths get a heading each. It does not sort, recurse or list individual
file operands. Its name buffer grows on demand. A directory mutation during
enumeration reports failure instead of restarting and potentially duplicating
already printed entries.

`mkdir path...` exclusively creates each final directory component. Parents must
exist; existing names are errors. It accepts trailing separators but does not
create intermediate directories. Success is silent. With no paths it prints usage
and fails.

Both accept native scheme and relative paths, treating all arguments as paths
without options. They report path-specific errors to stderr, continue to later
paths and return failure if any operation failed. Output errors stop the utility.
Directory operations use libpyxis; libc supplies allocation and output. The small
shared utility helper sizes path scratch storage and formats native errors without
introducing libc directory APIs or converting native statuses to errno. The libc
directory API [follow-up](technical-debt.md#directory-apis-in-libpyxis) remains open.

## Foreground shell

The packaged [shell](shell.md) uses libterm for command input and libpyxis for
navigation and synchronous foreground launch. Its quoting rules, builtin
commands and explicit startup/child authority are documented there. It does not
replace the normal boot programs yet; that is first-shell task 18.

## Foundational libc

Headers under [userspace/libc/include](../userspace/libc/include) define the
implemented subset: allocation, byte memory operations, string length/comparison/
search/duplication, integer/string formatting, environment lookup and
[unbuffered file/terminal stdio](stdio.md).
`snprintf`/`vsnprintf` report the full required length and terminate a nonempty
destination even when truncated. Their header lists supported formats; floating
point, wide characters and locale support are not implemented.
`errno` is process-local today because there is only one thread per process.
Native libpyxis calls continue to return native statuses without setting it.

The allocator separately compiles the project's pinned BSD-3-Clause TLSF source.
It retains its own MANAGE copy of the named startup memory grant, independent of
application handle close. A missing grant or failed copy leaves allocation
unavailable (`ENOMEM`) but still permits a program that needs no heap to run.
No backing is acquired until allocation needs it. Pools start at 64 KiB and grow
to suit larger requests, without a fixed pool count. Allocations have at least
16-byte alignment. Overflow/exhaustion return NULL; failed `realloc` preserves
the old allocation. Zero-sized allocations return NULL, and this libc defines
`realloc(p, 0)` to free p and return NULL.

`free` returns blocks to TLSF. Pools remain mapped until kernel process cleanup;
see [the retention tradeoff](technical-debt.md#retained-userspace-heap-pools).
Hello leaves its path workspace live until exit, while reusing freed file
buffers. Allocator assertions remain enabled and terminate the affected process
after allocation-free diagnostic logging.

## Switching and cleanup

Each CPU's scheduler runs on a permanent stack separate from task stacks. Before
entering a user task, it activates the process's address space and selects its
TSS and syscall-entry stack. First entry uses IRETQ; a preempted task resumes
through its saved interrupt frame. The local APIC timer selects runnable tasks
round-robin.

Userspace runs with interrupts enabled. Interrupt gates and SYSCALL disable
them on kernel entry, so syscall execution is not preempted on its own CPU and
a long syscall delays scheduling there. Endpoint CALL and RECEIVE explicitly
park the task while waiting; the scheduler resumes its private kernel stack and
address space before the handler accesses user memory again. Memory calls lend
the inactive private address space to the BSP for allocation/release; their
requests are published only after the caller has left its private root. Separate BSP
kernel tasks run with interrupts enabled and can be preempted. Other CPUs
continue running. SYSCALL needs an explicit kernel-stack switch; unlike an interrupt from userspace, it
does not load the stack from the TSS. Kernel GS identifies the current CPU,
with SWAPGS separating it from the user GS base on entry and return.

Task switches preserve general registers, x87/SSE state, segment selectors and
FS/GS bases. AVX and user FS/GS-base instructions are disabled; userspace must
stay within that supported feature set. Kernel code must not use FP/SIMD.

Exit and ordinary fatal user exceptions abandon the task's entry stack and
resume its scheduler. Only after switching to the permanent stack and kernel
root can the CPU return ownership to the BSP for cleanup. This prevents freeing
an executing stack or active page tables. The BSP then destroys the process
and its private address space, followed by the task's kernel stack and metadata.
The owning space and its TTY survive; original boot-module frames remain
reserved. Fatal kernel exceptions still panic. Normal libc exit flushes and
closes registered streams before invoking the kernel; _Exit and fatal faults
bypass that libc cleanup. No exit callbacks are implemented.
