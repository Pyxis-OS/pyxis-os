# Shell, filesystem and application runtime

The shell runs foreground programs with arguments, named resources and an
explicit working directory. It waits for completion, reports a nonzero exit or
user fault, and returns to the prompt. The default [init script](init.md) hands
off to an interactive shell at `home://` on CPU 1 when available, otherwise on
the BSP. See [the shell reference](shell.md) for commands, quoting, scripts and
a short walkthrough.

## Filesystem and authority

The kernel exposes directory and file objects through tagged synchronous calls
and process-local capabilities. `app://` selects the read-only initrd tree;
`home://` selects a shared writable RAM tree. Home contents survive process
exit but disappear on reboot. These names select explicitly granted startup
roots, not a global namespace available to every process.

Directory lookup returns an owned child handle with equal or reduced authority.
Enumeration lists names without granting handles. File reads and writes carry
explicit offsets; a file capability has no shared seek position. Removal or
replacement changes future name lookup while existing file handles retain their
objects. See [directories](directories.md) and
[filesystem mutations](filesystem-mutations.md) for rights and lifetime rules.

[Path helpers](paths.md) walk directory capabilities in userspace. Relative paths
use an owned directory chain, and `..` cannot cross its retained boundary.
Changing directory prepares a replacement chain before releasing the old one;
failure preserves the current context. A child receives its own grants and
cannot change its parent's working directory. Displayed paths and environment
strings describe context; they do not grant authority.

## Launch, startup and completion

A launcher capability authorizes creation in the caller's space, on its assigned
CPU. The caller supplies a readable program file, arguments, environment, named
resources, roots and directory context. There is no implicit capability-table
inheritance. Ordinary shell children receive the shell's explicit foreground
grants, including terminal access and filesystem roots, but not its launcher.
The `session` builtin explicitly delegates launch authority to its successor.

Launch prepares the image, private memory, startup record and grants before
making the child runnable. Failure unwinds unpublished resources without
changing the parent's handles. A successful launch returns a process observer;
waiting distinguishes ordinary exit from a fatal user fault and completes after
execution resources have been reclaimed. Closing an observer does not terminate
the child. See [processes](processes.md) and [script launch](script-launch.md).

C startup initializes the named resource snapshot before calling
`main(argc, argv)`. Arguments are writable; startup metadata and the initial
environment are read-only. Resource accessors borrow handles. `getenv` reads the
immutable environment; environment mutation is not implemented. Native startup,
image loading and C entry/exit are described in [userspace](userspace.md).

## Libraries and terminal use

| Library | Responsibility |
| --- | --- |
| libpyxis | Native object calls, startup accessors, capability paths and launch |
| libterm | Explicit-handle terminal I/O, dimensions, controls and line editing |
| libc | C startup/exit, allocation, memory/string functions, formatting and stdio |

The libraries live in the userspace repository and are exported through the
[SDK](sdk.md). Applications statically link the target libraries and compiler
runtime; no host libc enters the image. The userspace TLSF heap grows through
the caller-scoped [private-memory service](memory.md), independently of the
kernel heap. Kernel allocation and page-table mutation remain BSP-owned.

[Stdio](stdio.md) wraps file and console capabilities. Each file stream owns its
handle and offset; standard streams own copies of the named terminal grants.
Normal C exit closes streams, and kernel process cleanup reclaims remaining
handles and private memory. The separate `cat`, `ls` and `mkdir` utilities use
libc, with native directory operations where ISO C has no equivalent.

The kernel delivers terminal bytes without echo or line editing. [Libterm's
line editor](terminal.md) handles ASCII editing, cursor movement, wrapping and
Ctrl+C cancellation in userspace. The shell stops reading while waiting for its
foreground child. Text input and [physical-key sessions](keyboard.md) are
separate interfaces; global space navigation belongs to the session.

## Remaining boundaries

There are no background jobs, pipes, redirection, expansion, job control or
process cancellation. Foreground terminal handoff relies on cooperating
applications, without general reader/output ownership arbitration. History and
long-line viewports remain deferred. On a single CPU, Caelum logs share the TTY
and can disrupt editing. Runtime tradeoffs, including non-atomic stdio append,
are tracked in [technical debt](technical-debt.md).

Shell exit leaves its space, tab, terminal contents and shared roots alive;
there is no automatic restart. Persistent storage, mounts, overlays and
publication remain separate from the current filesystem. The [VFS draft](vfs.md)
and [space draft](spaces.md) describe future direction, not additional behavior
of the shell or runtime.
