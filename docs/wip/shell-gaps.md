# Shell conditions, merged output and script runtime

Status: **all three decisions accepted, 2026-10-18; implementation qualified for review.**
Scope: `&&`/`||`, `exit N`, `2>&1`, and Neovim `:make` launching a script through
`shell -c`. The owner accepted all three defaults on #946. Job provisioning,
background/service policy, general capability pipes and other
[everyday gaps](everyday-gaps.md) stay outside this task.

## Before implementation

Parent `31ca5009`, userland `3677edd2`, ports `33661036`. This table records the inspected pre-change boundary;
implementation qualification is in the [experiment record](../development/experiments/shell-gaps.md).

| Area | Current boundary |
| --- | --- |
| [Parser](../../userspace/shell/parse.c), [command execution](../../userspace/shell/command.c), [captures](../../userspace/shell/capture.c) | Only `;` sequences. Captures have a separate execution loop. Scripts and `-c` stop on failure; interactive `;` continues. |
| [Script loop](../../userspace/shell/script.c), [main](../../userspace/shell/main.c) | `exit` has no argument. Script command failure returns 1; `-c` preserves positive child exit status, otherwise 1. Native process exit is signed 32-bit, not an 8-bit status. |
| [Launch](../../userspace/shell/launch.c), [stdio](../userland/stdio.md#standard-streams-formatting-and-exit) | Each FILE-backed standard stream gets its own cursor. Two copies of a FILE grant do not merge stdout/stderr. Pipeline wiring and the transaction worker currently rely on physical stage positions. |
| [Script launch helper](../../userspace/lib/program.c) | `earns_extras` requires a held interpreter root and matching parsed shebang. Shell launch clears pipe/child-launcher extras and withholds threads without that root. |
| [Shell bundle](../../userspace/shell/manifest.json), [libuv](../../ports/libuv/pyxis/process.c), [Neovim](../../ports/neovim/runtime/sysinit.vim) | The bundled `-c` shell already receives its declared pipe/launcher and optional threads, but no `interpreter_root`. Neovim uses `shellpipe=2>%s` and `shellredir=2>%s`, collecting only stderr. |

## Owner decisions

### 1. Conditional-list execution and explicit status

**Accepted, 2026-10-18:** pipelines bind tighter than `&&` and `||`; those two have equal
precedence and evaluate left to right. `;` separates conditional lists. Parse the
whole line before effects, but expand only selected commands: skipped arms do
no substitution, CAP lookup, redirect OPEN or launch. Apply this in interactive,
script, `-c` and nested-capture execution, without adding grouping or negation.
Conditional lists are foreground-only; existing standalone background commands
and capture/builtin/paired-management restrictions remain.

A normal failed command, builtin or launch can select `||`. Conditions use the
existing pipeline result: the last user stage ordinarily, every stage in captures,
with no new `pipefail` policy. A fault reported by that policy stops the current
conditional list instead of selecting `||`; capture faults still stop capture.
Interruption, fatal wait/cleanup failure and explicit `exit` stop execution.
In scripts and `-c`, fail-fast applies after a whole conditional list, before
the next `;` or line. Interactive `;` continues. Thus:

```text
./build.pxs && ./app.pxe
./build.pxs || echo 'build failed'; echo 'handled'
```

The second line continues if its handler succeeds; `fail && skipped; next`
stops a script because the list failed. Fail-fast uses the whole list result;
there are no additional `errexit` exceptions.

Add `exit [N]`: omitted N stays 0; one strict decimal integer, optionally minus,
must fit signed 32-bit. No byte masking. An invalid argument reports builtin
failure without exiting an interactive shell. Scripts and `-c` return explicit
N; cleanup errors are reported, change requested 0 to failure and preserve a
requested nonzero status. Implicit failure status keeps the current script/`-c`
rules above. Terminal completion for a valid exit
remains builtin acceptance; the shell process exit carries N.

A handled command failure such as `x=$(fail || echo recovered)` may produce a
successful capture. Recovery never clears an output mutation failure, failed
handed-off resource, retention overflow or uncertain transaction. Unhandled
byte-producing failure binds a failed value with no successful prefix. A handed-off
value retains its own outcome even when its producer exits nonzero, as today;
command status and resource outcome remain distinct.

### 2. Ordered `2>&1` with one byte sink

**Accepted, 2026-10-18:** add only exact `2>&1` for foreground external stages. Install
pipeline defaults first, then apply redirects in written order. Duplication
captures stdout's current sink; a later stdout redirect does not retarget it.
Keep existing duplicate-destination, builtin/background and other fd restrictions.
Unavailable or unwritable stdout refuses preparation; it never finds another sink.

| Command | Result |
| --- | --- |
| `cmd > file 2>&1` | Both outputs reach one file writer/cursor. |
| `cmd 2>&1 > file` | Stderr keeps the original stdout; only stdout goes to file. |
| `cmd 2>&1 \| next` | Both outputs feed the pipeline. |
| `cmd 2>&1 > file \| next` | Stderr feeds the pipe; stdout goes to file. |
| `cmd > $api 2>&1` | Both byte streams form one staged request body; normal commit/failure rules apply. |

Share existing PIPE/CONSOLE endpoints directly, preserving ordinary wait,
Ctrl+C and launcher behavior when no relay is needed. For a FILE or capture sink,
use one bounded pipe relay holding the sole output cursor; descendants inherit
the pipe, so successive script commands cannot restart that file cursor. Reuse
the transaction staging worker when it already owns the sink. Refuse a
captured transaction whose stderr would write bytes to the reply collector
before the reply handoff (`$(cmd 2>&1 > $api)`); `$(cmd > $api 2>&1)`
merges into the body and remains supported. Honor explicit
and inherited append policy, without promising atomic append or ordering beyond
actual writes and application buffering. Like ordinary pipe EOF, a descendant
retaining a writer delays completion; existing subgroup cancellation applies,
without a new timeout or change to background/service ownership.

Explicit merged output is byte-oriented: seek fails, and a capability handoff
is unsupported on the merge pipe. `cat` uses its existing definite-unsupported
byte-copy path; failed reads/relay writes remain failures. Captures without
`2>&1` keep their typed handoff and retention behavior. A copied FILE capability
or a child-local descriptor alias alone is insufficient across descendants.

Workers count against the existing eight-process batch bound. Keep logical
pipeline stages distinct from helper entries: multiple redirected stages may
need separate sinks, not an appended linear pipeline. Preflight capacity before
outer redirect OPEN/truncation/launch; already executed substitutions are not
rolled back. Close shell-held writers, drain owned descendants and check helper
completion before capture publication/commit. Helper success cannot replace the
last user stage's status; helper failure fails the command. Preparation errors
stay on the shell's own stderr; no implicit retry or rollback is added.

Update Neovim's collection options to `>%s 2>&1` in the same delivery, so `:make`
and redirected shell commands collect stdout as well as stderr.

### 3. Runtime-only script re-entry, separate from interpreter trust

**Accepted, 2026-10-18:** split ordinary runtime forwarding from the trusted-root path in the
shared script-launch helper. A bundled shell without `interpreter_root` retains
a READ-only native interpreter image from its own held startup `app` directory.
For the exact parsed Pyxis-shell shebang, use that held image and pass it privately
to nested script shells. It is a code-selection capability, **not proof of trust**;
no caller can turn it, a URI spelling or a rebound `boot` entry into an
`interpreter_root`. Missing/unusable image refuses runtime re-entry; no mutable
scheme lookup substitutes for it.

The runtime-only offer consists solely of the caller's declared pipe CREATE,
its subgroup-bound child launcher with LAUNCH, and optional threads MANAGE, with
masks narrowed to what it actually holds. Memory/clock/random follow the current
runtime profile. Select from retained startup grants, preserve lineage and
collision refusal, and never acquire absent rights. Missing optional threads
still yields the ordinary named required-resource error when Clang needs it.
Do not extend plain native programs or other interpreters with these extras.

**Only trusted roots extend the trusted interpreter chain.** That path keeps
its pinned archive lookup and private root delegation. Runtime interpreter-image copies keep source lineage and
are released with their invocation. The runtime-only path
adds no trusted root, job provisioner, retention factory, session/device authority
or unbound ancestor launcher. It runs inside the existing owned `-c`/script
subgroup, so stop/exit drains its descendants. Ordinary backgrounds, services
and named bootstrap scripts keep their existing policies. The Neovim/libuv
caller needs no command-name exception or new grant beyond bundle declarations.

## Delivery

Owner-approved order, 2026-10-18: decision 1, then 3, then 2.

- [x] **Conditions and exit status.** Userland parser/execution/capture loops,
  then parent pin and reference updates.
  **After this task the owner can:** use conditional builds and explicit script
  exit statuses.
- [x] **Runtime-only interpreter re-entry.** Userland shared launch helper,
  held-image forwarding and nested ownership, then parent pin/reference updates.
  **After this task the owner can:** run `:make` through `./build.pxs` with the
  runtime authority already delegated to the editor, including Clang threads.
- [x] **Merged output.** Userland bounded sink relays and ordered `2>&1`, Neovim
  collection-option ports PR, then parent pins/reference updates.
  **After this task the owner can:** collect both compiler streams in quickfix.

Dependency PRs start from current mains, remain fast-forwardable and land before
parent pins; signed commits, no trailers. No kernel or compiler-container change
is required. Baseline before code: repeated existing Lua/native-job small launches,
`iobench` pipe throughput and script/`-c` completion, then interleaved matched
runs at the same CPU count. Measure relay cost separately; no before-time exists
for formerly unsupported syntax. Keep native and nested results distinct.

Qualification uses ordinary builds, interactive QEMU and debugger inspection:
selected/skipped branches and substitutions, chain failures and explicit exit
bounds; ordered/pipeline/captured/append merges and helper refusal/failure;
Neovim `:make` collecting both streams and reporting build status; nested scripts
with/without threads, rebound roots, missing interpreter image, ordinary-program
nondelegation and Ctrl+C/exit subtree cleanup. Inspect held masks and absence of
trusted extras. No new test infrastructure or native result is claimed here.

Implementation: userland [#291](https://git.internal/PyxisOS/pyxis-userland/pulls/291)
→ [#292](https://git.internal/PyxisOS/pyxis-userland/pulls/292)
→ [#293](https://git.internal/PyxisOS/pyxis-userland/pulls/293), then ports
[#127](https://git.internal/PyxisOS/pyxis-ports/pulls/127), then the parent pins.
The [shell](../userland/shell.md), [script launch](../userland/script-launch.md)
and [Neovim](../userland/neovim.md#external-jobs-and-shell-commands) references
describe the implemented contract. See the
[experiment record](../development/experiments/shell-gaps.md) for measured
nested controls, real Neovim `:make` qualification and limits.
