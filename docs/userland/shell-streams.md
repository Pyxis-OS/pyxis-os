# Standard streams, redirection and pipelines

Pyxis programs use independent stdin, stdout and stderr bindings backed by
console, FILE, PIPE or STREAM capabilities. The shell supplies file redirection and
foreground pipelines, and `cat` and `head` consume the selected streams without
knowing their backing. The shell-streams milestone is complete.

The following examples use `boot://share/hello.txt` from a `DISTRIBUTION=full`
development image. The default core archive omits it; use another readable
text file there.

```text
cat boot://share/hello.txt > home://copy.txt
cat < home://copy.txt | head -n 2
cat boot://ls.pxe | head -c 16 > home://prefix
cat missing 2> home://errors | cat > home://empty
```

## Stream authority and ownership

Launch stages ordinary `stdin`, `stdout` and `stderr` environment entries with explicitly selected grant authority. Each stdin
handle has READ authority; stdout and stderr have WRITE authority. A stream's
FILE grant may be native or [exported by a provider](../interfaces/file-providers.md); exported
streams additionally carry CALL transport, while native streams carry zero.
HTTP stdin selects a completed READ-only FILE snapshot without an observation
companion. Launch validates and copies only the selected stream authority, never
transaction control or body authority.
The kernel validates the selected source grants. Libc checks each captured stream
interface and its masks, adopting owned LOOKUP grants and releasing the temporary
snapshot before application execution. Each FILE has a non-owning descriptor
association invalidated before close or number reuse. An original standard
descriptor conditionally removes its still-matching environment publications;
newer bindings and dup aliases are preserved. A FILE wrapper keeps no extra pipe
owner alive.
Missing bindings remain unavailable with EBADF; they do not become EOF or fall
back to another output or the terminal. Named terminal capabilities are separate.
See [stdio](stdio.md#standard-streams-formatting-and-exit) and
[process launch](../interfaces/processes.md) for the interfaces.

File positions belong to each libc descriptor. Copying a grant does not create a
shared position; stdout and stderr targeting the same file can overwrite one
another.
Console, pipe and generic STREAM input are sequential. HTTP FILE snapshots
support positioned reads, SIZE and independent child cursors. `fread` retains
fill-request semantics;
`fread_some` returns bytes already read ahead or one available backend transfer,
with sticky EOF/error indicators, and never reads ahead. Cat uses it to forward
terminal and pipe data promptly while retaining bulk file reads. Buffered stdio
input may read ahead on file, pipe and response streams; those bytes stay in the reading
process and do not accompany a delegated stream. See
[input read-ahead](stdio.md#input-read-ahead).

Native launch consumers use `pyxis_stdio_stream` to borrow the current live
FILE binding while its owner remains live. No immutable startup stream snapshot
exists. Descriptor-number reuse does not reconnect a closed FILE wrapper.

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
group supervision or implicit pipe-creation authority is added (a script run by
`boot://init-shell.pxe` alone also receives pipe creation, as its pipelines need it), and plain
programs launched in the background or as services gain no launcher. A bundle
declaring `launcher` is offered it in every launch mode. Shebang adaptation adds no
other authority. See
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
revisit points are retained in [technical debt](https://git.internal/PyxisOS/pyxis-os/issues/1441).

Completed HTTP GET uses [typed OPEN](../interfaces/http-transactions.md) and the
provider's redirect/storage policy. Writable typed opens return generic
transactions. `api=$(http prepare POST URI)` captures configuration;
`reply=$(producer > $api)` stages, checks producers and commits once, retaining
the completed FILE under `reply`. `cat $reply` reads it without a new GET.
Uncaptured transaction output follows the same worker/commit checks and discards
a successful response. General capability pipes remain C9.
