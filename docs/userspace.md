# Userspace

## Building an image

`make userspace` first builds the [SDK](sdk.md), then the freestanding
applications against it. Each application has a directory and an explicit target
in the [userspace Makefile](https://git.internal/PyxisOS/pyxis-userland/src/branch/main/Makefile); after `make sdk`, for example,
`make -C userspace SDK=../build/sdk BUILD=../build/userspace hello` builds just that program. Pass `CROSS_COMPILE` as for
kernel builds and `HOSTCC` for the converter. Outputs live under `build/`.

Runtime libraries and startup are built separately by
[userspace/runtime.mk](https://git.internal/PyxisOS/pyxis-userland/src/branch/main/runtime.mk) into `build/runtime`, then exported
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
[linker script](https://git.internal/PyxisOS/pyxis-userland/src/branch/main/linker.ld) separates segment permissions on page
boundaries; writable executable mappings are rejected. The
[P1F header](../include/pxe/p1f.h) defines the image layout and constraints.
Keep the ELF for debugging; the converted image does not replace its symbols.
See [gdb.md](gdb.md) for kernel debugger usage.

## Entry and loading

`make initrd` packages the selected [init](init.md), shell, cat, ls, mkdir,
[Kilo and its license](ports.md), and a
sample text file into `build/initrd.cpio`; `make image` includes that uncompressed `newc` archive as the
sole Limine module. A boot-time [tree builder](../include/kernel/fs/initrd_tree.h)
exposes its entries as read-only directory/file objects without extracting file
contents or adding a block device. The text asset is `share/hello.txt`.

The image packages the endpoint client and server examples. From the shell,
`session app://server.pxe` launches a server and two clients with explicit
startup grants; `--wide` exercises full-size payloads and four attachments,
and `--abandon` demonstrates receipt closure. See [endpoints](endpoints.md).

The image loader creates an inactive private address space, copies the program
into owned backing and applies its permissions. Failure releases partial
allocations; loading does not change the caller's active address space.

The kernel supplies an executable entry point and a writable, non-executable,
16-byte-aligned user stack. Boot and subsequently launched processes each get
1 MiB of eager stack backing at `0x800000`, with the initial stack pointer at
`0x900000`. The page immediately below the backing (`0x7ff000`) is reserved
without a mapping, so later allocations cannot consume it. Images overlapping
either the stack or guard are rejected. Process destruction releases both.
Stack growth is not implemented. The guard catches accesses into that page;
it cannot catch a large stack adjustment that skips over it.
Entry receives a pointer in `RDI` to the
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
[startup_resource() and the other native accessors](https://git.internal/PyxisOS/pyxis-userland/src/branch/main/include/startup.h)
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
Caelum and serial. On multicore boots use Super+Right to select CPU 1 before typing;
input on Caelum is discarded. The single-CPU fallback accepts input on Caelum,
where kernel logs can disrupt the editor's display. See [keyboard input](keyboard.md)
and [libterm](terminal.md) for input and editing behavior.

The [console wrapper](https://git.internal/PyxisOS/pyxis-userland/src/branch/main/include/console.h) reports bytes written and
its helpers finish partial writes. The kernel renders bounded chunks under the
output lock. Terminal input is blocking, with no EOF convention.

CALL takes a handle, a tagged message and its size, then a reply buffer and
capacity. Shared protocol headers define the tag and operation payloads. Rights are
checked against the handle's object type, so the same bit may mean console
WRITE or file READ. Kernel and userspace are rebuilt together against the
shared ABI headers. Older layouts are not supported.

The [file wrappers](https://git.internal/PyxisOS/pyxis-userland/src/branch/main/include/file.h) query size, read/write at explicit
offsets, resize and synchronize files. FILE payloads contain copied bytes rather
than caller buffer addresses. Each wrapper performs one bounded transfer: reads
return at most 4,088 bytes and writes submit at most 4,080 bytes, leaving room for
metadata within the 4 KiB payload limit. Larger requests may return short counts;
callers advance by confirmed progress and continue with the remaining suffix.
The wrappers preserve native error statuses and validate reply lengths/counts.
RAM writes complete the submitted chunk or leave the file unchanged; other
backends may report partial progress or an uncertain mutation, which must not be
automatically retried. Gaps and newly grown ranges read as zero. Zero bytes with
nonzero read capacity means EOF. Cat uses libc stdio to copy bytes without assuming
NUL-terminated content or allocating a buffer the size of the file. The helpers
use bounded stack storage and require valid caller buffers; copying adds no heap
or memory-service dependency. The [file-provider bridge](file-providers.md) routes exported files through
endpoint invocation and shares OPEN resolution with libc and shell redirection.

The [handle wrapper](https://git.internal/PyxisOS/pyxis-userland/src/branch/main/include/handle.h) releases the calling process's
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

Headers under [userspace/libc/include](https://git.internal/PyxisOS/pyxis-userland/src/branch/main/libc/include) define the
implemented subset: allocation, byte memory operations, string length/comparison/
search/copy/duplication, numeric conversion/formatting, environment lookup and
[unbuffered file/terminal stdio](stdio.md).
`snprintf`/`vsnprintf` report the full required length and terminate a nonempty
destination even when truncated. Their header lists supported formats, including
floating point. Wide characters and locale support are not implemented.
`errno` is process-local today because there is only one thread per process.
Native libpyxis calls continue to return native statuses without setting it.

`strtol`, `strtoul`, `strtoll` and `strtoull` accept ASCII whitespace/signs and
bases 2–36, with base 0 selecting the radix. They include C23 binary prefixes;
see [C23 draft 7.24.1.7](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n3096.pdf#page=379).
Range errors saturate and set `ERANGE` while consuming the full digit sequence.
No conversion leaves the end pointer at the input; successful calls leave
`errno` unchanged. Invalid bases return zero with `EINVAL`. `atoi` provides
decimal conversion without reliable range diagnostics. No locale state or
floating conversion is added by these functions.

`qsort` is an in-place, unstable heapsort: O(n log n) comparisons, constant
stack use, no allocation or recursion. Comparators receive pointers to array
elements and return a negative, zero or positive result.

`assert` evaluates its expression once and writes the expression, file, line
and function to stderr before calling `abort` on failure. Defining `NDEBUG`
disables evaluation; re-including `<assert.h>` honors its current setting.
`abort` terminates through `_Exit(EXIT_FAILURE)`, without flushing streams or
running libc cleanup. There is no signal delivery or abort handler.

`setjmp`/`longjmp` provide nonlocal recovery within a thread. `setjmp` returns
zero initially; `longjmp` resumes that call with the supplied value, replacing
zero with one. Use `setjmp` as a whole controlling expression, optionally with
`!` or a comparison against an integer constant, or as a standalone statement;
do not use it in an initializer or assignment.

The buffer must refer to the most recent save in a still-active invocation of
the saving function. Do not jump across threads or back into a scope of a
variably modified type after leaving it. Non-volatile automatic locals in the
saving function that changed since the save have indeterminate values after
the jump. Heap objects and streams are not unwound; recovery code owns cleanup.
The x87/MXCSR control modes and status remain as they were at `longjmp`, following
[C23 7.13.2.1](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n3096.pdf#page=301).
This libc saves only the callee-saved integer registers, stack pointer and return
address; it adds no signal-mask handling or kernel context-switch interface.

The `<math.h>` subset provides `floor`, `fmod`, `pow`, `frexp`, `ldexp`, `scalbn`,
`fabs`, `scalbnl`, `ldexpl`, `fmodl`, `fabsl`, `copysignl` and `frexpl`, built from
pinned musl sources. They are in libc and need no `-lm`. The SDK uses SSE2
float/double evaluation and x87 80-bit long double. `floor` rounds downward;
`fmod` and `fmodl` use a quotient truncated toward zero. Scaling follows the
active rounding mode. Errors use floating-point exception flags
(`math_errhandling` is `MATH_ERREXCEPT`) and leave `errno` unchanged. Traps are
masked at process start; there is no public fenv interface yet. `<math.h>` also supplies `INFINITY`, `NAN`
and the `HUGE_VAL`/`HUGE_VALF`/`HUGE_VALL` constants. The SDK carries musl's license
and the subset's provenance under `share/licenses`. Signaling NaN support and a
full math library remain outside this subset.

`strtof`, `strtod` and `strtold` use the same pinned musl subset with a direct
string reader. They accept ASCII whitespace/signs, decimal and hexadecimal
numbers, case-insensitive `inf`/`infinity` and `nan` with an optional alphanumeric
or underscore payload. The decimal separator is always `.`; there is no locale
state. NaN payload text is consumed but does not select payload bits or sign.
Incomplete exponents and NaN payloads leave the end pointer at the end of the
valid prefix. If no conversion occurs, the result is zero, the end pointer
remains at the original input and errno is preserved. The end-pointer argument
may be NULL.

Conversion rounds for the requested float, double or 80-bit long-double type.
Overflow, any nonzero subnormal result (even an exact one), and nonzero input
rounded to zero set `ERANGE`; ordinary conversions, including literal infinity
and NaN, preserve errno. This conversion errno contract is separate from the
math helpers' FP-exception-only convention above. The decimal scanner uses an
8 KiB automatic workspace without heap allocation; it keeps rounding information
and consumes remaining digits when that workspace fills. Floating-point printf
is described in [stdio](stdio.md#standard-streams-formatting-and-exit); a full
libm remains deferred.

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
address space before the handler accesses user memory again. Memory and display
calls lend the inactive private address space to the BSP for mapping changes; their
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

## Floating point

Userspace uses baseline x86-64 x87/SSE2 instructions and the System V floating
calling convention. `float` and `double` arithmetic normally use SSE; `long
double` uses the 80-bit x87 format in 16-byte ABI storage. The compiler and SDK
enable this by default, with no red zone. Kernel code, including libraries built
into the kernel, must retain `-mgeneral-regs-only`.

Each task owns an aligned FXSAVE64 area. New tasks start with empty x87 registers,
zero XMM registers, x87 control word `0x037f` and MXCSR `0x1f80`: round to nearest,
masked FP exceptions, and no flush-to-zero/denormals-are-zero mode. Preemption
and blocking save this state; dispatch restores it before returning to the task.
Syscalls preserve it even when they park. AVX/XSAVE state is not supported;
do not compile for a newer CPU baseline or enable AVX.

Static libgcc provides compiler arithmetic/conversion helpers. This does not
provide a full libm. The libc conversion/scaling subset is described
[above](#foundational-libc); floating-point output uses the
[shared printf formatter](stdio.md#standard-streams-formatting-and-exit).
There is no public floating-point environment API yet.

Run `mandelbrot` from the shell to render a double-precision Mandelbrot set into
a [mapped pixel buffer](graphics.md). It displays progress below the navigation
bar and waits for a terminal key before releasing graphics and restoring the TTY.
