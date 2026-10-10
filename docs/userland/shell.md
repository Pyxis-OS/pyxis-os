# Shell

`make userspace` builds the SDK, shell and core utilities. Normal boot runs the
default [init script](init.md) and [session launcher](session-configuration.md),
which by default start separate shells in the Development and Read-only spaces,
with shared `home://` as their working directory. Boot starts on Caelum's tab;
use Super+Right to select Development before typing. The normal initrd contains init,
shell, ls, tree, cat, echo, head, mkdir, rm, rmdir, mv, [Kilo and its license](../development/ports.md), and `share/hello.txt`;
home is initially empty. On live boots it is RAM and its contents disappear on
reboot; installed systems keep it in the pool's `home` volume.

A first walkthrough:

```text
ls boot://
cat boot://share/hello.txt
mkdir notes
cd notes
ls
cd boot://share
cat hello.txt
exit
```

On ordinary exit, process cleanup logs completion in Caelum and releases the shell's
resources. Its space, tab, framebuffer contents and shared namespace roots
remain alive. There is no automatic restart or new input consumer in that
space, but tab switching and kernel presentation continue. In the single-CPU
fallback, Caelum logs and shell output share a TTY and can disrupt line editing.

An optional `screen_capture` resource is preserved through session successors,
service launches, foreground and background commands, and every pipeline stage.
It carries CAPTURE alone, with no transport rights, independently of DRAW and
filesystem grants. A shell without it grants none. The
[`screenshot` boot policy](init.md#boot-configuration) controls which spaces
receive it; [screenshot PATH](screenshot.md) saves a PNG through that authority.

## Commands and quoting

The shell uses libterm's line editor and waits for the complete foreground
command or pipeline. An interactive shell launched with `--no-echo` uses the
quiet editor instead: no prompt, input echo, redraw, cursor or style control,
submission newline or `^C` is written, while editing, cancellation, EOF and the
prompt-derived line limit are unchanged. Application output and shell
diagnostics are unaffected, and the option is never passed to children or
session successors. Other interactive-shell arguments are rejected; the remote
server selects `--no-echo` for machine clients that request it. The prompt shows the working path, for example
`tmp://notes> `. Long paths show an ellipsis and their tail, keeping at least half the first row for
input; control and non-ASCII bytes display as `?`. Whitespace separates
arguments. Single and double quotes preserve whitespace and allow empty
arguments; adjacent quoted/unquoted
pieces form one argument. Outside single quotes, backslash takes the following
character literally, including inside double quotes. Inside single quotes,
backslash is an ordinary character. For example:

```text
mkdir 'two words'
cd two\ words
ls
cd boot://share
cat "hello.txt"
```

Unfinished quotes or escapes report an error without executing anything. An empty
line does nothing. Ctrl+C at the prompt cancels the current line; while a
foreground command runs it [interrupts the command](#interrupting-foreground-commands). Input
loss discards the line; a submitted line that hit the editor's buffer/display
limit is also rejected. The command buffer has room for 1023 bytes plus NUL;
libterm's visible-area limit may be smaller.

Up and Down recall earlier lines into the editor, where they can be edited
before Enter runs them; Down past the newest line brings back what was being
typed. Each interactive shell keeps its own last 100 submitted lines in memory,
leaving out empty and all-space lines and a repeat of the line before. A
`--no-echo` shell and scripts keep none.

**Saved history.** Lines are saved in `home://.history`, one file per home,
shared by every shell of every space that can write that home.
- **Loading:** a shell loads the newest 100 saved entries when it starts. It
  doesn't see lines other running shells save later; a new shell, including a
  `session` successor, picks them up.
- **Saving:** each line the shell records is saved before it runs. The shell
  merges it into the newest file contents, writes them to a new
  `.history.HEX` file and renames that over `.history`.
- **Concurrent shells:** they interleave in the order they saved. Two saves
  within the same few milliseconds can lose one line, because the later
  rename wins.
- **Crashes:** a crash or kill loses at most the line being saved. It may leave
  one `.history.HEX` file, which is safe to delete when no shell is running.
- **No sync:** a native volume flushes the moved file before a replacing rename
  commits, and a RAM home has nothing to make durable.
- **Private lines:** a line starting with a space stays in memory, so Up still
  recalls it, but is never saved.

**File format.** One entry per line, ending in LF, printable ASCII and at most
1023 bytes.
- **Bounds:** the file keeps the newest 1000 entries within 64 KiB.
- **Loading:** it skips anything else, and the next save rewrites the file
  without it.

**No writable home.** A space without `home://`, or with a read-only one such
as Read-only's, still loads what it can read but saves nothing, without a
message. The first failed save prints one `shell: history not saved` line, and
that shell stops saving.

Searching history with Ctrl+R is [deferred](../technical-debt.md#initial-terminal-editor).

**Tab completion.** Tab completes the command name: the first word of the line
or of a pipeline stage after `|`. The candidates are the shell builtins and the
programs in `bin://` and `boot://` (each `NAME.pxe` offered as `NAME`), sorted
and without duplicates. Names that need quoting to type are not offered, and
`bin://` entries added by a development bundle catalog are not either.

- **One match** replaces the word and adds a space.
- **Several matches** extend the word to their common prefix when that is longer.
  Otherwise Tab lists them in columns under the line and draws the prompt and
  line again. Tab on an empty line lists every command.
- **Nothing** happens in argument position, after a redirection operator, inside
  quotes, or when the word contains `/`, `:` or another byte a bare name cannot
  hold. Paths and arguments are not completed.
- A completion that would exceed the line limit changes nothing and marks the
  line as full. The shell lists `bin://` and `boot://` with its own grants; if
  either cannot be listed, it offers fewer names.

The local shell, a [multiplexer](multiplexer.md) pane and the
[remote terminal](remote-terminal.md) shell complete the same way; the quiet
(`--no-echo`) editor and script mode do not. The editor interface is
[`term_read_line_completing`](terminal.md#tab-completion).

There is no expansion, substitution or globbing. `$`, `*` and `;` remain literal
argument bytes. Unquoted `<`, `>` and `2>` select file redirection as described
below; `|` connects foreground external commands as described under pipelines.
Unsupported operator combinations are errors. Interactive input treats `#` literally; script mode supports whole-line comments.

`cd path` changes the shell's owned directory chain. It requires one argument;
failure preserves the old chain, its rights and the displayed path. `exit` takes
no arguments and ends the shell successfully. Interactive command failures do
not accumulate into the shell's exit status; unrecoverable terminal, wait or
cleanup failures terminate it with failure.

With an optional `terminal_events` grant, the interactive root shell reports one
ordered, typed completion for each nonblank submitted line. Foreground external
work reports after the existing waits: the last pipeline stage's exact exit code,
fault or termination. External preparation or launch failure, including a
missing image or unopenable redirect, reports launch failure. A successful
background command reports launch, not its later exit. Recognized builtins,
including invalid builtin arguments, report builtin success (0) or failure (1);
`session` and `service` remain builtin transactions even though they launch
processes, and `exit` or a successful `session` handoff reports success before
the shell leaves. Parse errors, command-form errors such as a builtin in a
pipeline or with redirection, and submitted line-limit rejection report
rejection. Diagnostics are written before the completion. This outcome is
reporting only: script stopping, `exit` and fatal handling are unchanged.
Blank lines below the line limit, cancellation, lost input and EOF without
submission emit nothing; submitted line-limit rejection takes precedence.
Fatal command handling emits nothing; failed event emission ends the shell with
failure and is never retried. The event grant remains local to this interactive
shell and is never forwarded to children, scripts or session successors.
See the [remote interface](remote-terminal.md#client-modes) for framing and
persistent machine-client use.

`title [--optional] name` sets the current space's tab label; quote names with
spaces, for example `title "Source editing"`. It accepts 1–63 printable ASCII
characters. An error leaves the old title unchanged. `--optional` only
tolerates a missing title grant. Titles survive shell exit, and only session
handoff inherits the grant by default. See [space titles](init.md#space-titles).
Like the other builtins, `title` cannot run with `&`.

`affinity LIST` narrows the CPUs the current space's tasks may use, for example
`affinity 2-3` or `affinity 1,3`. It needs the affinity grant that only a
space's trusted init receives, and it works only before the space's first
launch. See [affinity setup](init.md#affinity-setup). Indices above 8191 are a
usage error. Like the other builtins, `affinity` cannot run with `&`.

A command without `/` is a bare name: `lspci` opens `bin://lspci.pxe`, and when
that is not found, `boot://lspci.pxe`. Installed systems keep ordinary programs
in `bin://` and the rescue set in `boot://`; live boots bind `bin://` to the
archive. There is no PATH variable. Names already ending in `.pxe` still
receive the suffix when bare; use `bin://cat.pxe` or `./cat.pxe` to name an
image directly. Paths
containing `/` are resolved as written. Builtins are recognized after quote
removal. An empty command name is an error. Opened programs use the
[script-launch helper](script-launch.md), which can dispatch a shebang to a native
interpreter.

[`ls [-1] [-l] [--] [directory...]`](ls.md) sorts directory names by byte order.
Console output uses colored columns fitted to its width; pipes and files keep
plain one-name-per-line output. `-1` forces lines, and `-l` shows kind, byte size
and name with no time column. Directory sizes are `-`; unavailable file sizes
are `?`, with diagnostics and failure status. Unknown options are usage errors.

`echo [-n] [ARG...]` is a native external program. It prints arguments separated
by one space, followed by a newline. An exact first argument `-n` suppresses the
newline; later `-n` arguments and all other option-like text are literal. It
does no escape processing: shell quoting determines the argument bytes. With no
arguments it prints a newline; `echo -n` writes nothing. Output errors report
failure. Redirects and pipelines work as for other external commands:

```text
echo hi > home://note.txt
echo a b | cat
echo -n x
```

`rm path...` removes files only; `rmdir path...` removes empty directories only.
Both accept multiple literal paths, continue after an individual failure, and
return failure if any removal failed. They have no options, recursive removal,
parent pruning or wildcard expansion. Existing handles survive removal, and
removing an empty directory makes it unavailable for new children even through
an older handle. See [the directory contract](../interfaces/directories.md#removal).

`mv [--] source... destination` renames files with libc `rename`: to the
destination path, replacing an existing file, or, when the destination is a
directory, into it under each source's last name. Several sources need a
destination directory. Directory moves and cross-filesystem copying are
unsupported. It continues after a failed source and reports failure without
deleting the source or destination itself. See [mv](mv.md).

[`cp [--] source-file... destination`](cp.md) copies files within or across roots.
A directory destination receives each source's basename; several sources require
an existing directory. Existing files are replaced through a completed sibling
temporary file, so the destination directory needs create, file-write and remove
rights. There is no recursive copy or direct-truncation fallback. Read failures
preserve the old destination; unconfirmed publication reports possible names
without retrying or deleting them. See the reference for concurrency and cleanup
limits.

`sync path...` requests synchronization for each explicit file or directory
path, in argument order. It continues after an individual error and exits with
failure if any target failed; with no arguments it prints usage and fails.
Success is quiet. For example, after Kilo closes a saved file, run
`sync host://work/hello.c host://work` to request file and parent-directory
synchronization. The command resolves each path when invoked, so a concurrent
rename can make it sync a different object. Native callers that must sync the
same object they already hold can use libpyxis `file_sync(handle)` or
`directory_sync(handle)`. Syncing one target does not sync the whole filesystem.
See [the host synchronization contract](../devices/virtio-fs.md#synchronization) for
permissions, errors and durability limits.

## Service publication

Init scripts can create and populate an explicit [service namespace](../interfaces/namespaces.md):

```text
namespace create
service start counter bin://counter.pxe --provide
counter --lookup counter
namespace remove counter
```

`service replace NAME IMAGE [ARG...]` atomically replaces an existing binding
after the provider transfers an exported client through IPC. Existing clients
keep their original object. `namespace remove` does not withdraw an export.
Ordinary commands receive namespace LOOKUP only; trusted session handoff can
retain held management authority. Provider publication launches omit the parent
namespace. FILE providers expose ordinary read-only snapshots through the
[shared file bridge](../interfaces/file-providers.md).

`service start --optional` continues after a provider explicitly reports setup
failure; malformed handshakes, launch failures, namespace errors and cleanup
failures are still errors. `service start|replace --read-only` restricts the
launched provider's native directory roots and working directories to traversal
and file reads. HTTPS boot startup uses both options; custom trust is selected
with `httpfs --https --ca-bundle URI`. See [HTTPS startup](http-fetch.md).

## File redirection and stdin

Foreground external commands accept `< file`, `> file` and `2> file`. For example:

```text
cat boot://share/hello.txt > home://copy.txt
cat < home://copy.txt
cat missing 2> home://errors.txt
cat home://errors.txt
mkdir notes
cd notes
cat < ../copy.txt > "another copy.txt"
```

Redirects follow the command name and may appear between arguments. Whitespace
around operators is optional: `cat<copy.txt>another.txt` works. A redirect consumes
one nonempty filename word using the ordinary quotes/escapes; quoted or escaped
operators are literal. `2>` selects stderr only when the unquoted `2` begins a
word and immediately precedes `>`: `cat 2 > out` instead passes a file operand
`2` and redirects stdout. Other descriptor prefixes, missing filenames, duplicate
redirects for one stream and leading/standalone redirects are rejected.

`>>`, `<<`, `<>`, `2>&1` and other unsupported operator combinations are
errors. Builtins (including `session`) and background commands reject redirects.
The whole command is parsed and these restrictions checked before opening files.
Interactive commands and scripts use the same redirection rules.

Paths use the shell's current directory and root grants. The shell first opens the
executable, then opens all redirect targets in written order. Input must exist;
output is opened or created without truncation. After wiring pipes, the shell
compares every explicit output with every stage's final FILE stdin, including
inherited input. Different known domains prove distinctness even without object
IDs; within one domain both IDs must be available and different. An alias,
unknown domain, missing required identity or failed query aborts before any
truncation. See [file metadata](../interfaces/file-metadata.md).
Only after this check are outputs truncated, in written order, and children launched. The child
receives independent native file grants, not filenames to reopen. Temporary shell
handles close after launch or failure. `<` withholds keyboard and pointer grants.
If stdout remains a console, the final foreground stage receives named terminal
input with READ alone, so a pager can read keys independently of its file stdin.
The separate terminal-output grant remains available for explicit terminal
operations. Unredirected standard streams retain their inherited bindings.

A syntax error or missing executable path does not touch redirect targets. A later
open failure can leave newly created files, but existing outputs have not yet been
truncated. Once truncation starts, a resize error, malformed executable, missing
shebang interpreter, allocation or launch failure may leave outputs truncated.
There is no rollback or atomic multi-file update. Shell preparation errors use the
shell's own stderr; `2>` redirects the child's stderr only.

`cat < file > alias` is protected even through different root/path spellings.
Argument files are outside the check: `cat file > file` can still destroy its input.
stdout and stderr each start at offset zero, even for `> out 2> out`; their
independent writes can overwrite one another. This is not a merged output stream.

`cat` with no operands reads stdin to EOF. A `-` operand reads stdin among ordinary
file operands; repeated `-` continues the same stream without closing or rewinding
it. Use `./-` for a file literally named `-`. Cat uses `fread_some` for every input:
terminal and pipe data is copied as it becomes available, without waiting to fill
its transfer buffer. File operands and file-backed stdin keep bulk reads and
normal EOF. Terminal input remains raw and blocking. Remote machine-client stdin
EOF sends END_INPUT, which drains queued terminal bytes before EOF; the local
framebuffer console has no input EOF operation. Ctrl+D is an input byte rather
than a stream closure. Ctrl+C at the prompt cancels shell editing; during a
running cat it terminates cat. No options
or terminal line discipline are added.

## Bounded input with head

```text
head [-n N | -c N] [file|-]
```

Head copies the first ten lines by default. `-n N` selects a line count; `-c N`
selects a byte count. An omitted input or `-` reads stdin; otherwise it opens one
file through the ordinary path grants. Options precede the file operand. Use
`--` before a filename beginning with `-`; `./-` names a literal dash file.

Counts contain decimal digits only, from zero through UINT64_MAX. Signs,
suffixes, attached option values, repeated/conflicting count options and multiple
input files are rejected before opening input. A zero count performs no reads or
stdout writes; an explicit file is still opened, so invalid paths fail normally.
Open/close errors still fail, including closing an unavailable standard binding.

Line mode counts newline bytes and preserves all bytes, including CR and NUL.
A final unterminated line is copied unchanged; EOF before the requested count is
successful. Input closes at the limit, EOF or an I/O error, before a final output
write or error diagnostic can block. Errors in
reading, writing or closing report to stderr and return failure.

Head never consumes past its selected boundary. Line mode reads one byte at a
time, at the cost of more native calls for long lines; byte mode uses
`fread_some` with at most 4 KiB and never more than its remaining count. Neither
mode dispatches on the input backend. Line output is staged until a newline,
EOF/error or 4 KiB; byte output forwards each available read.

For example:

```text
cat boot://share/hello.txt | head -n 2
cat bin://tcc.pxe | head -c 16 > home://prefix
head -c 0 home://copy.txt
```

Closing the reader early discards unread pipe bytes and makes further upstream
writes fail with EPIPE. The shell reports earlier-stage failures while retaining
the last stage's result. There are no multiple-file headers or additional head
options. Remote terminal END_INPUT can end either mode before its requested limit;
local framebuffer input needs the requested newlines/bytes or an input error.
Ctrl+D does not close either input stream.

## Foreground pipelines

Connect two through eight external commands with `|`:

```text
cat boot://share/hello.txt | cat > home://copy.txt
cat < home://copy.txt | cat | cat
cat missing 2> home://errors.txt | cat > home://empty.txt
```

Whitespace around `|` is optional. Quotes and escapes keep it literal. Every
stage needs a nonempty command name; leading, trailing or adjacent pipes are
errors. `||`, `|&`, background pipelines and builtin stages (`cd`, `exit`, `mount`,
`title`, `session`, `poweroff`, `reboot`) are rejected before any file is opened. Redirections retain
their ordinary per-stage syntax and duplicate-stream checks. Interactive input
and script lines use the same rules and their existing total line-length limits.

By default stdout flows to the next stage's stdin. The first stage inherits the
shell's stdin, the last inherits its stdout, and each stage inherits stderr
separately. Explicit redirects override that stage's defaults:

- `cat source > saved | cat` writes `saved`; the second cat receives EOF.
- `cat source | cat < other` reads `other`; the first cat has no pipe reader and
  gets EPIPE if it writes.
- `cat missing 2> errors | cat` writes child errors to `errors`, leaving the
  pipeline's byte stream independent of stderr.

The first stage, when its selected stdin is a console, receives named terminal
input, keyboard and pointer grants as before. The final foreground stage also
receives named terminal input with READ alone when its stdin is a pipe or file
and its stdout is a console. It receives no interrupt-arming, keyboard or pointer
right through this exception. This lets `ls boot:// | less` read keys separately
from the pipe; readers can hold Ctrl+C passthrough with READ authority.

There is one console input queue. A pipeline whose first stage reads console
stdin while its final stage also reads console keys, such as `cat | less`, has
competing readers and is unsupported. Use a named file, redirected input or a
producer that does not read the console. Intermediate stages, a final stage with
non-console stdout, background jobs and services gain no input through this rule.
Separate terminal-output and display capabilities retain the
ordinary child policy. Programs explicitly using those capabilities can still
write to or draw on the terminal. In spaces that opt into
[ordinary child launch](init.md#boot-configuration), each foreground stage
receives LAUNCH through the shell's separate `child_launcher` grant. It receives
no CREATE_GROUP or pipe-creation authority; shebang adaptation does not add
authority.

The shell parses and validates the entire pipeline, then opens all executable
images before opening redirect targets. It opens redirects in written order
across stages, creates the needed pipes and prepares grant storage, then truncates
outputs in written order. The batch launcher prepares every child before making
any runnable. A preparation failure starts none and identifies the failing stage
where available. Files created or truncated before failure remain changed, with
the same aliasing and independent-position limits described above.

Children receive only their selected endpoints. Unused ends close without being
delegated; after launch the shell closes every temporary pipe, image and redirect
handle before waiting. This lets a consumer reach EOF after its writers finish,
and a producer observe EPIPE after its readers finish.

The shell waits for every child and closes every observer before prompting.
Diagnostics identify stages, numbered from one, with nonzero exits or faults.
The **last stage**
determines pipeline success; an earlier failure does not override a successful
last stage. There is no pipefail option. In scripts, a failed last stage stops
the script, while an earlier failure followed by a successful last stage permits
the next line. Wait, cleanup and diagnostic I/O errors remain fatal to the shell.
An unknown launch outcome is also fatal because terminal input cannot safely
resume.

There is no job control. Ctrl+C terminates a running foreground command or
pipeline, as described below. Remote terminal END_INPUT supplies EOF after queued input;
local framebuffer input has no EOF operation. A child that waits on live terminal
input or ignores its pipe can keep the shell waiting even after its peers finish.
Disconnecting a remote session terminates its entire execution group.

## Interrupting foreground commands

A root shell, or a session successor, can stop its foreground job with Ctrl+C.
It receives the [interrupt right](terminal.md#interrupt-arming-and-passthrough)
on its `input` grant: from the kernel's initial console input through init,
`session` and the startup script locally, and from the terminal-create input
remotely. `session` launches pass the right on, because the successor replaces
the shell. Ordinary commands receive READ alone, so they cannot arm or observe
interrupts. Scripts that hold the right arm their own foreground commands in
the same way.

For each foreground job, the shell:

1. Arms Ctrl+C before launching any stage, so a press during startup never
   reaches a stage as data.
2. Waits for every stage's completion and the interrupt together, through
   `wait_many` with 30-second deadlines from the shell's clock.
3. On Ctrl+C, requests [TERMINATE](../interfaces/processes.md) for every stage
   and stops watching the interrupt. It stays armed, so further presses are
   discarded instead of reaching a stage.
4. Collects results as before, then disarms before editing the next line.

Diagnostics and typed completion are unchanged. Each interrupted stage reports
`Process terminated`, and a pipeline's completion follows its last stage. A
stage that finished before its termination took effect keeps its real result.

Termination covers only the foreground stage processes launched by the shell.
The shell does not supervise their descendants. A child launched by Lua can
outlive an interrupted, exited or faulted Lua process; Ctrl+C does not terminate
that child. Remote disconnect still terminates the whole remote execution group.

Input typed before Ctrl+C is discarded, so text typed into a hung command never
runs as the next shell command. Ctrl+C at the prompt still cancels the line,
because the shell is not armed while editing.

For a foreground graphical job, Super+Down shows the terminal and routes typing
to its normal 4 KiB queue while the game keeps running. The shell still waits for
the job and opens no second prompt; unread text waits for a later reader.
Super+Up restores graphics and clears unread terminal bytes. While the terminal
is shown, Ctrl+C reaches the shell's existing armed interrupt and terminates the
foreground job through the same authority and cleanup above. This adds no job
control or termination authority. See [graphics layers](../interfaces/graphics.md#choosing-the-visible-layer).

Programs that read lines through libterm hold passthrough only while editing a
line. In the Lua REPL, Ctrl+C cancels a typed line, but running code is
terminated. Kilo holds passthrough for its whole session, so Ctrl+C never ends
it or discards unsaved edits; quit with Ctrl-Q.

- **Background, service and session launches** are never armed.
- **Without the right or a clock**, jobs wait without interruption, as before.
- **If arming fails**, the shell prints one diagnostic and runs the job without
  interruption.
- **Raw-keyboard programs** such as Doom receive key events rather than text
  while capture is the destination. Use their own controls, or show a foreground
  graphical job's terminal with Super+Down and use the armed Ctrl+C interrupt.

## Background commands

An unquoted, unescaped trailing `&` launches an external command without waiting:

```text
udp-echo 127.0.0.1 9000 --count 1 &
```

Whitespace before `&` is optional. Quoted `"&"` and escaped `\&` remain argument
bytes. A bare `&`, `&&`, or text after an unquoted `&` is a syntax error; nothing
on that line runs. Built-ins (`cd`, `exit`, `mount`, `session`) reject background
execution.

Background children receive no `input` or `keyboard` grant, so they cannot read
the shell's terminal or physical-key stream. Their standard input is unavailable,
not a fabricated EOF stream. Output, display and other ordinary child resources
are retained; output may interrupt the prompt, and there is no coordinated
redraw. The shell closes its process observer immediately. Existing process
cleanup reclaims the child's execution resources when it exits.

There is no job table, completion notification, exit-status collection, `jobs`,
`fg`/`bg`, signal delivery or termination command. Ctrl+C interrupts only the
foreground job, never a background command. Use programs with their own bounded exit policy.
Background launch also works in scripts: launch failure stops the script, while
successful launch lets it continue regardless of the child's eventual result.
Ungrouped local children may outlive the shell. The remote server terminates all
remaining session descendants when its root shell exits or the client disconnects.

Successful launch does not guarantee application readiness. For a UDP server,
wait for its listening message before sending traffic. Nothing starts in the
background during default boot.

## Script mode

A named READ resource `script` selects script mode. `argv[1]` supplies the name
used in diagnostics; the shell reads the granted handle from offset zero rather
than reopening that name. Additional arguments are not expanded by the shell.
It reads one command per line with the same parser, `cd`, `exit` and foreground
execution as interactive mode, without prompts or command echo. Terminal input
stays available to foreground children.

Blank lines and whole-line `#` comments are ignored, including the shebang.
Spaces/tabs may precede the comment marker; embedded or quoted `#` is literal.
Each line allows 1024 bytes before LF, including CR in CRLF. Only CR immediately
before LF is stripped. A final line without LF is executed too. Oversized lines
and embedded NUL bytes are rejected without executing that line; comments have
the same bounds. A trailing backslash is an error, not line continuation.

The first malformed command, failed `cd` or launch, nonzero foreground-command
exit, foreground-command fault or read failure stops the script with failure.
For pipelines, the last stage determines success as described above. Shell
diagnostics include
`script-name:line:` (lines start at one); child diagnostics retain their own
format. EOF or `exit` succeeds. Neither falls back to an interactive prompt.

Script mode needs the same startup resources as interactive mode, including an
explicit launcher grant. In a space with `launch = true`, an ordinary foreground
shell script receives LAUNCH as `launcher`; its other required startup resources
must still be present. It does not receive `child_launcher`, so its own ordinary
commands receive no launcher. This does not provide general nested shell-script
execution. Without the opt-in, an ordinary shell script still fails its launcher
resource check. Boot explicitly grants the init interpreter launch authority;
the default init uses `session` to pass it to the configuration launcher, which
then delegates it to the interactive shell.

## Mounting roots

`mount [--optional] [--read-only | --read-write] host` uses init's scoped
`host_mount` resource to open the selected export and bind `host://` in this
shell. Access defaults to read-only; write grants authorize mutation attempts
but do not establish backend or host writability. It rejects an
existing binding. Optional mode skips only a missing resource; any actual mount
failure is an error, which stops a script. Ordinary shells receive the mounted
root rather than mount authority. See [host setup and lifetime](../devices/virtio-fs.md).

Native volume mounts use a separate init resource:

```text
mount [--optional] [--no-info] --partition N --volume NAME --read-only NAME://
mount --partition 1 --volume system --read-only data://
ls data://
cat data://share/hello.txt
data://bin/program.pxe
```

The `native_mount` authority selects the configured disk and bootstrap principal;
`--partition` is a positive one-based GPT entry number on that disk. The counted
volume name is 1–255 UTF-8 bytes and resolves once within the retained generation.
The binding name is chosen separately, so `data://` need not match the volume
name. The command requires `--read-only`; it acquires LOOKUP, ENUMERATE and
READ_FILES, plus FILESYSTEM_INFO when its mount authority holds OBSERVE.
`--no-info` explicitly withholds that observation grant. The lower-level mount ABI
can request a narrower root with LOOKUP,
rejects unknown rights and returns READ_ONLY for known mutation rights.

Destination validation, collision checks and binding storage reservation precede
acquisition. A conflicting root or service name fails without replacing it;
service collisions are checked again after acquisition, with the unpublished
root closed on failure. At most 16 roots are selected, including app/home/HOST;
overflow fails explicitly. Successful roots are added to the shell's explicit
selection and forwarded to children. Mount authorities are excluded from that
selection. No unmount, host refresh or hotplug administration is supplied.

`--optional` permits only a missing native authority, from disabled configuration
or confirmed hardware absence. Failures from present authority remain errors,
including wrong disk GUID, ambiguous/unusable hardware, invalid GPT/filesystem,
missing partition/volume and policy denial. See
[boot configuration](init.md#native-disk-configuration-and-mounting).
Observation grants permit the [scoped information query](../interfaces/directories.md#scoped-filesystem-information)
without granting content access. Read-only session forwarding preserves observation
when held; it cannot restore an omitted grant.

## Session handoff

`session program [arguments...]` launches a successor in the same space and on
its assigned CPU, then exits the calling shell successfully without waiting.
For example, an init script can finish with `session boot://shell.pxe`. The
command also works interactively; failed launch returns to the prompt, while
script mode reports the script name/line and exits with failure as usual.
Program lookup and quoting use the ordinary command rules, including shebang
launch. The `session` word is removed from the child's arguments.

The successor receives copies of the usual terminal and memory grants, the
explicit selected root list and working-directory grants, current working-path
metadata and current environment, plus an explicit `launcher` resource preserving
the caller's LAUNCH and any CREATE_GROUP authority. An optional `child_launcher`
is forwarded separately with LAUNCH alone. Other startup
resources, including the caller's `script`, `host_mount` and `native_mount`, are
not forwarded. Launching another script supplies that target's own READ script
grant through `program_launch`.

Successful launch ends script execution immediately: later lines do not run,
and the caller never reads terminal input again. Closing its process observer
and exiting releases only the caller's references; the successor keeps its own
references and can read input and launch programs after the caller is reclaimed.
There is no wait for application readiness: success means the launch was
accepted, not that the new program will initialize successfully. A later exit or
fault does not bring back the original shell or restart the session.

This is explicit delegation followed by caller exit, not process replacement
or a terminal ownership protocol. It does not add supervision or `exec`.

In a remote session, root-shell exit causes the server to terminate all remaining
group members, including a session successor. Remote `session` handoff therefore
cannot keep a successor running after that shell exits.

## Power-off and restart

`poweroff` and `reboot` are builtins that take no arguments. They need the
shell's `power` resource: local spaces opt in with `power = true`, and remote
root shells require the separate `remote_power = true`
([boot configuration](init.md#boot-configuration)). Live/PXE Remote enables it;
installed defaults do not. Without the grant they print
`this space has no power authority`.

The kernel stops running user programs without asking them to exit, writes every
mounted native pool's cached data and empties its journal, then powers off
through ACPI or restarts. Files written without `sync` survive; RAM volumes such
as `tmp://` do not. If a pool cannot be flushed, the command reports the status,
programs resume and the system stays up. Holding the physical power button
still switches off immediately, keeping only synced data. See
[ACPI power-off](../kernel/acpi.md#power-off-and-restart).

## Startup and child authority

The shell expects named `input`, `output`, `memory` and `launcher` resources,
plus `boot` and `tmp` roots. Input/output are separate console READ/WRITE grants;
memory permits MANAGE and launcher permits LAUNCH. Normally app supplies LOOKUP,
ENUMERATE and READ_FILES, and home additionally supplies CREATE, WRITE_FILES and
REMOVE. Directory grants can be restricted by the launcher; the shell queries
and preserves their actual rights.
An optional `power` resource supplies the `poweroff` and `reboot` builtins. The
shell forwards it only to a `session` successor, never to the programs it runs;
session passes it on to local successors but not when it starts remote services.
The separate `remote_power` resource also passes only to session successors;
the remote supervisor converts it to `power` for its root shell. Ordinary
commands and service providers receive neither.
An optional `display` resource supplies DRAW authority for the space. An optional
`clock` resource supplies READ and SLEEP authority for monotonic time. An optional
`keyboard` resource supplies INPUT authority for physical-key sessions.
Additional selected roots, including optional HOST and native mounts, retain
the access granted by init. Root names do not determine permissions. The shell
preserves at most 16 startup roots and fails explicitly if that limit is exceeded.

An optional `child_launcher` resource controls delegation to ordinary foreground
commands, including every pipeline stage. The packaged Development and installed
`pyxis` profiles provide it; Read-only and Remote do not. When present, the shell
passes this grant as `launcher` with LAUNCH alone. It never substitutes its own
launcher or adds CREATE_GROUP, and background commands and services gain no
launcher through this policy. Trusted `session` handoff preserves
`child_launcher` as a distinct resource. If Remote explicitly opts in, that grant
is bound to the remote session's execution group.

The shell uses libc's retained [working-path context](process-state.md), seeded
from startup with its navigation boundary. Unknown descriptive spelling leaves
lookup usable and shows `[cwd unavailable]` in the prompt. The path is not resolved to replace the
chain: actual handles remain authoritative, and insufficient grants
fail normally. With no initial chain the shell starts at `tmp://`. Explicit
scheme changes use the bound root's actual grant; each descendant lookup retains
its parent's grant. Crossing a retained ancestor boundary fails as in the native
path API.

Each child receives independent standard-stream grants selected from the shell's
bindings, explicit redirects and pipe connections, with their declared
console/file/pipe protocols. Background children omit stdin.
Missing streams remain absent; stderr never falls back to stdout or the terminal.
The script interpreter and session handoff preserve these bindings too.

Each foreground child receives explicit copies of terminal output, memory and
the explicitly selected roots with their actual rights and transport masks, and
the current directory chain preserving each handle's rights independently.
Keyboard and pointer grants require the first stage's console stdin. Named
terminal input additionally follows the final-stage rule above. Optional ordinary
launch authority follows the separate `child_launcher` policy above. When
available, the [display](../interfaces/graphics.md),
[clock](../kernel/timekeeping.md), [random](../devices/randomness.md) and [keyboard](../devices/keyboard.md) grants are also forwarded
to eligible foreground children and session successors; background children omit keyboard input.
An owned snapshot of libc's current environment is forwarded explicitly, with
the existing network overlay. No shell assignment command or automatic PWD
maintenance is added. Children receive current working-path metadata alongside
independent directory grants. Display normalization removes redundant separators
and dot components; lookup still walks the original input, so `missing/..` fails.

The shell never reads terminal input while waiting. Successful wait means child
resources have been reclaimed; it closes the observers, reports nonzero exits or
faults, and prompts again after every foreground child has completed. The terminal advances to a fresh line
only when its cursor is not already at column zero, preserving unterminated child
output without inserting an extra blank line after newline-terminated output.
Failed launch returns to the prompt; failed wait ends the shell because
input ownership can no longer be assumed. Fatal user faults are reported through
the existing completion kind, with details left in the kernel log. No descendant
supervision or terminal ownership mechanism is added.
