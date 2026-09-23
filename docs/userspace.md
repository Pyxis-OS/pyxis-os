# Userspace

## Building an image

`make userspace` first builds the [SDK](sdk.md), then the freestanding
applications against it. Each application has a directory and an explicit target
in the [userspace Makefile](https://git.internal/chronium/pyxis-userland/src/branch/main/Makefile); after `make sdk`, for example,
`make -C userspace SDK=../build/sdk BUILD=../build/userspace hello` builds just that program. Pass `CROSS_COMPILE` as for
kernel builds and `HOSTCC` for the converter. Outputs live under `build/`.

Runtime libraries and startup are built separately by
[userspace/runtime.mk](https://git.internal/chronium/pyxis-userland/src/branch/main/runtime.mk) into `build/runtime`, then exported
to `build/sdk/sysroot/usr/lib`. Applications link the SDK's startup object,
libc, libpyxis and libterm archives, plus compiler-provided libgcc. Only referenced
archive objects are pulled in. No host runtime is linked. Libpyxis owns native
operations and startup accessors; libc owns C entry/exit, allocation and the
initial C support routines.

To convert an already linked executable:

```sh
build/sdk/bin/elf2pxe --format p1f -o hello.pxe hello.elf
```

The input must be a fixed-address, little-endian x86_64 ELF executable with no
interpreter, dynamic linking, runtime relocations or TLS. The
[linker script](https://git.internal/chronium/pyxis-userland/src/branch/main/linker.ld) separates segment permissions on page
boundaries; writable executable mappings are rejected. The
[P1F header](../include/pxe/p1f.h) defines the image layout and constraints.
Keep the ELF for debugging; the converted image does not replace its symbols.
See [gdb.md](gdb.md) for kernel debugger usage.

## Entry and loading

`make initrd` packages the selected [init](init.md), shell, cat, ls, mkdir and a
sample text file into `build/initrd.cpio`; `make image` includes that uncompressed `newc` archive as the
sole Limine module. A boot-time [tree builder](../include/kernel/fs/initrd_tree.h)
exposes its entries as read-only directory/file objects without extracting file
contents or adding a block device. The text asset is `share/hello.txt`.

Hello, client and server remain optional build targets, for example
`make -C userspace SDK=../build/sdk BUILD=../build/userspace hello client server`. They are not packaged or launched by
normal boot and still require their example-specific startup grants.

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
[startup_resource() and the other native accessors](https://git.internal/chronium/pyxis-userland/src/branch/main/include/startup.h)
to find supplied handles and environment values. Lookup borrows an existing
handle and never duplicates it. Missing resource names return HANDLE_INVALID;
missing environment values return NULL, while an empty value is an empty string.
`getenv` borrows these same immutable values; environment mutation is not
implemented. The shell receives the read-only `app` root, shared RAM-backed
`home` root, terminal input/output, launcher and caller-scoped
[private-memory service](memory.md). It starts at `home://`, with an empty RAM
tree, and explicitly supplies grants, directory context and environment when
launching foreground children. See [the shell contract](shell.md).

Normal output targets the owning space's TTY; the kernel-log syscall targets
Caelum and serial. On multicore boots use Alt+Right to select CPU 1 before typing;
input on Caelum is discarded. The single-CPU fallback accepts input on Caelum,
where kernel logs can disrupt the editor's display. See [keyboard input](keyboard.md)
and [libterm](terminal.md) for input and editing behavior.

The [console wrapper](https://git.internal/chronium/pyxis-userland/src/branch/main/include/console.h) reports bytes written and
its helpers finish partial writes. The kernel renders bounded chunks under the
output lock. Terminal input is blocking, with no EOF convention.

CALL takes a handle, a tagged message and its size, then a reply buffer and
capacity. Shared protocol headers define the tag and payload union. Rights are
checked against the handle's object type, so the same bit may mean console
WRITE or file READ. Kernel and userspace are rebuilt together against the
shared ABI headers. Older layouts are not supported.

The [file wrappers](https://git.internal/chronium/pyxis-userland/src/branch/main/include/file.h) query size, read/write at explicit
offsets and resize RAM files. They preserve native error statuses and check reply
lengths/counts. Writes complete in full or leave the file unchanged; gaps and
newly grown ranges read as zero. A short read is allowed and zero bytes with
nonzero capacity means EOF. Cat uses libc stdio to copy bytes without assuming
NUL-terminated content or allocating a buffer the size of the file.

The [handle wrapper](https://git.internal/chronium/pyxis-userland/src/branch/main/include/handle.h) releases the calling process's
reference. A closed handle is immediately stale; other owners, including the
space that owns the console, retain their references. Process exit releases
handles left open. The read-only startup record is not updated after close.

The [boot launcher](../kernel/user/launch.c) prepares one shell on CPU 1 when
available, otherwise on the BSP. Both boot
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
commands and explicit startup/child authority are documented there. On shell
exit its process is reclaimed, completion is logged in Caelum, and the space
and terminal contents remain visible. No shell restart is performed.

## Foundational libc

Headers under [userspace/libc/include](https://git.internal/chronium/pyxis-userland/src/branch/main/libc/include) define the
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
Allocator assertions remain enabled and terminate the affected process
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
