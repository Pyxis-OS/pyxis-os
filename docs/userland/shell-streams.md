# Standard streams, redirection and pipelines

Pyxis programs use independent stdin, stdout and stderr bindings backed by
console, file or pipe capabilities. The shell supplies file redirection and
foreground pipelines, and `cat` and `head` consume the selected streams without
knowing their backing. The shell-streams milestone is complete.

```text
cat boot://share/hello.txt > home://copy.txt
cat < home://copy.txt | head -n 2
cat boot://tcc.pxe | head -c 16 > home://prefix
cat missing 2> home://errors | cat > home://empty
```

## Stream authority and ownership

Launch selects dedicated stream grants with explicit protocol tags. Each stdin
handle has READ authority; stdout and stderr have WRITE authority. A stream's
FILE grant may be native or [exported by a provider](../interfaces/file-providers.md); exported
streams additionally carry SEND|RECEIVE transport, while native streams carry zero.
The kernel checks the interface and both authority masks. A stream's
grant index cannot also be used by another stream or an ordinary startup binding.
Libc descriptors adopt those handles directly, without leaving unused startup
copies. Each FILE has a non-owning association that is invalidated before the
descriptor closes or its number is reused; the wrapper cannot keep a pipe alive.
Missing bindings remain unavailable with EBADF; they do not become EOF or fall
back to another output or the terminal. Named terminal capabilities are separate.
See [stdio](stdio.md#standard-streams-formatting-and-exit) and
[process launch](../interfaces/processes.md) for the interfaces.

File positions belong to each libc descriptor. Copying a grant does not create a
shared position; stdout and stderr targeting the same file can overwrite one
another.
Console and pipe streams are sequential. `fread` retains fill-request semantics;
`fread_some` returns bytes already read ahead or one available backend transfer,
with sticky EOF/error indicators, and never reads ahead. Cat uses it to forward
terminal and pipe data promptly while retaining bulk file reads. Buffered stdio
input may read ahead on file and pipe streams; those bytes stay in the reading
process and do not accompany a delegated stream. See
[input read-ahead](stdio.md#input-read-ahead).

`startup_stream()` returns an immutable borrowed snapshot. It keeps no native
reference alive. After its owning descriptor closes, the snapshot must not be
used or forwarded to a child; descriptor-number reuse does not refresh it.
Existing launch consumers may borrow it only while that owner remains live.

## Pipe and launch lifetime

[Native pipes](../interfaces/pipes.md) have fixed 64 KiB storage and a 4 KiB per-call transfer
limit. Readers wait only while empty with writers remaining. Final-writer
closure lets buffered data drain before EOF. Writers wait when full; final-reader
closure wakes them with EPIPE. All affected waiters are awakened on peer closure.
Shared-storage bookkeeping does not retain endpoint ownership. Copied endpoints
keep their direction open until every copy closes; release uses normal deferred
kernel cleanup.

[Batch launch](../interfaces/processes.md#batch-launch) prepares one through eight images,
grant tables, startup regions, task stacks and WAIT observers before publication.
It validates reply storage and prepares ordered results first. Failure starts no
children, removes provisional observers and preserves caller source handles;
where applicable, it identifies the failing request index. Publication is
infallible and makes all children runnable, without simultaneous-start semantics.
Published children have independent lifetimes and closing an observer does not
terminate a child.

## Shell behavior and bounded consumption

The [shell guide](shell.md#foreground-pipelines) defines quoting, per-stage
redirection and last-stage success. The shell checks the whole command line and
opens every image before redirect targets, opens redirects before truncating,
and closes temporary endpoints before waiting for every child. Redirects override
pipe defaults. The first stage with console stdin receives named terminal input,
keyboard and pointer grants. The final foreground stage receives named terminal
input with READ alone when stdin is a pipe or file and stdout is a console;
no keyboard, pointer or interrupt-arming right is added. Programs such as
[less](less.md) use this grant for keys independently of the pipe. Both stages
reading the console, as in `cat | less`, is unsupported because they share one
input queue. In a space with `launch = true`, every ordinary foreground stage
receives LAUNCH from the shell's separate `child_launcher` resource. No
CREATE_GROUP or pipe-creation authority is added, and background commands and
services gain no launcher. Shebang adaptation does not add authority. See
[startup and child authority](shell.md#startup-and-child-authority).

[Head](shell.md#bounded-input-with-head) copies ten lines by default, or the
selected line/byte count from one input. It stops at the exact requested boundary
and closes input promptly. Line mode reads one byte at a time; byte mode uses
bounded bulk reads. Early reader exit can make an upstream writer fail with EPIPE.
The shell reports that failure, but the last stage still determines pipeline
success. A successful pipe write alone never proves that its bytes were consumed.

## Limits

There is no job control, background pipeline, builtin pipeline, pipefail option
or local terminal EOF convention. [Ctrl+C](shell.md#interrupting-foreground-commands)
terminates the directly launched foreground stages; it does not supervise or
terminate their descendants. A child launched by Lua can outlive Lua's
interruption, exit or fault.
A child waiting on terminal input or doing unrelated work can keep the shell
waiting indefinitely after peers finish. Pipes have no message boundaries,
guaranteed atomic write size, strict fairness, nonblocking mode or wait sets.

File creation/truncation before a launch failure is not rolled back. Redirecting
output onto input can destroy the input, including through aliases. Independent
file positions do not provide descriptor-duplication semantics. These limits and
revisit points are retained in [technical debt](../technical-debt.md#shell-redirection-side-effects-and-file-aliases).

Future provider-to-shell capability handoff and explicit request submission
remain separate designs in [userspace scheme providers](../wip/userspace-scheme-providers.md#prepared-requests-and-shell-handoff).
FILE provider opens are implemented through the shared library bridge. Prepared
requests, implicit submission and additional authority remain outside it.
