# Shell

`make userspace` builds the SDK, shell and core utilities. Normal boot runs the
default [init script](init.md) and [session launcher](session-configuration.md),
which by default start separate shells on CPUs 1 and 2 when present,
or one on the BSP for a single-CPU boot, with shared `home://` as their working directory. On multicore boots
use Super+Right to select CPU 1 before typing. The normal initrd contains init,
shell, ls, cat, head, mkdir, rm, rmdir, mv, [Kilo and its license](ports.md), and `share/hello.txt`;
home is initially empty and its RAM
contents disappear on reboot.

A first walkthrough:

```text
ls app://
cat app://share/hello.txt
mkdir notes
cd notes
ls
cd app://share
cat hello.txt
exit
```

On ordinary exit, process cleanup logs completion in Caelum and releases the shell's
resources. Its space, tab, framebuffer contents and shared namespace roots
remain alive. There is no automatic restart or new input consumer in that
space, but tab switching and kernel presentation continue. In the single-CPU
fallback, Caelum logs and shell output share a TTY and can disrupt line editing.

## Commands and quoting

The shell uses libterm's line editor and waits for the complete foreground
command or pipeline. The prompt shows the working path, for example
`home://notes> `. Long paths show an ellipsis and their tail, keeping at least half the first row for
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
cd app://share
cat "hello.txt"
```

Unfinished quotes or escapes report an error without executing anything. An empty
line does nothing. Ctrl+C cancels the current line, not a running child. Input
loss discards the line; a submitted line that hit the editor's buffer/display
limit is also rejected. The command buffer has room for 1023 bytes plus NUL;
libterm's visible-area limit may be smaller. History and scrolling input beyond
that visible area remain deferred.

There is no expansion, substitution or globbing. `$`, `*` and `;` remain literal
argument bytes. Unquoted `<`, `>` and `2>` select file redirection as described
below; `|` connects foreground external commands as described under pipelines.
Unsupported operator combinations are errors. Interactive input treats `#` literally; script mode supports whole-line comments.

`cd path` changes the shell's owned directory chain. It requires one argument;
failure preserves the old chain, its rights and the displayed path. `exit` takes
no arguments and ends the shell successfully. Interactive command failures do
not accumulate into the shell's exit status; unrecoverable terminal, wait or
cleanup failures terminate it with failure.

`title [--optional] name` sets the current space's tab label; quote names with
spaces, for example `title "Source editing"`. It accepts 1–63 printable ASCII
characters and clips the visible label to the fixed tab width. An error leaves
the old title unchanged. `--optional` only tolerates a missing title grant, as
on the single-CPU Caelum fallback. Titles survive shell exit, and only session
handoff inherits the grant by default. See [space titles](init.md#space-titles).
Like the other builtins, `title` cannot run with `&`.

A command without `/` is a bare name: `cat` opens `app://cat.pxe`. There is no
PATH search or fallback. Names already ending in `.pxe` still receive the suffix
when bare; use `app://cat.pxe` or `./cat.pxe` to name an image directly. Paths
containing `/` are resolved as written. Builtins are recognized after quote
removal. An empty command name is an error. Opened programs use the
[script-launch helper](script-launch.md), which can dispatch a shebang to a native
interpreter.

`rm path...` removes files only; `rmdir path...` removes empty directories only.
Both accept multiple literal paths, continue after an individual failure, and
return failure if any removal failed. They have no options, recursive removal,
parent pruning or wildcard expansion. Existing handles survive removal, and
removing an empty directory makes it unavailable for new children even through
an older handle. See [the directory contract](directories.md#removal).

`mv source-file destination-file` renames a file and replaces an existing file
at the exact destination path. It accepts exactly two operands, has no options,
and does not append a basename when the destination is a directory. Directory
moves and cross-filesystem copying are unsupported. It uses libc rename and
reports failure without deleting the source or destination itself.

`sync path...` requests synchronization for each explicit file or directory
path, in argument order. It continues after an individual error and exits with
failure if any target failed; with no arguments it prints usage and fails.
Success is quiet. For example, after Kilo closes a saved file, run
`sync host://work/hello.c host://work` to request file and parent-directory
synchronization. The command resolves each path when invoked, so a concurrent
rename can make it sync a different object. Native callers that must sync the
same object they already hold can use libpyxis `file_sync(handle)` or
`directory_sync(handle)`. Syncing one target does not sync the whole filesystem.
See [the host synchronization contract](virtio-fs.md#synchronization) for
permissions, errors and durability limits.

## Service publication

Init scripts can create and populate an explicit [service namespace](namespaces.md):

```text
namespace create
service start counter app://counter.pxe --provide
counter --lookup counter
namespace remove counter
```

`service replace NAME IMAGE [ARG...]` atomically replaces an existing binding
after the provider transfers an exported client through IPC. Existing clients
keep their original object. `namespace remove` does not withdraw an export.
Ordinary commands receive namespace LOOKUP only; trusted session handoff can
retain held management authority. Provider publication launches omit the parent
namespace. FILE providers expose ordinary read-only snapshots through the
[shared file bridge](file-providers.md).

`service start --optional` continues after a provider explicitly reports setup
failure; malformed handshakes, launch failures, namespace errors and cleanup
failures are still errors. `service start|replace --read-only` restricts the
launched provider's native directory roots and working directories to traversal
and file reads. HTTPS boot startup uses both options; custom trust is selected
with `httpfs --https --ca-bundle URI`. See [HTTPS startup](http-fetch.md).

## File redirection and stdin

Foreground external commands accept `< file`, `> file` and `2> file`. For example:

```text
cat app://share/hello.txt > home://copy.txt
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
output is opened or created without truncation. Only after every target is open
are output files truncated, in written order, and the child launched. The child
receives independent native file grants, not filenames to reopen. Temporary shell
handles close after launch or failure. `<` withholds terminal input and keyboard
grants; the separate terminal-output grant remains available for explicit terminal
operations. Unredirected standard streams retain their inherited bindings.

A syntax error or missing executable path does not touch redirect targets. A later
open failure can leave newly created files, but existing outputs have not yet been
truncated. Once truncation starts, a resize error, malformed executable, missing
shebang interpreter, allocation or launch failure may leave outputs truncated.
There is no rollback or atomic multi-file update. Shell preparation errors use the
shell's own stderr; `2>` redirects the child's stderr only.

There is no same-file protection: `cat file > file` and `cat < file > file` truncate
the input before it is consumed. Different path spellings may alias the same file.
stdout and stderr each start at offset zero, even for `> out 2> out`; their
independent writes can overwrite one another. This is not a merged output stream.

`cat` with no operands reads stdin to EOF. A `-` operand reads stdin among ordinary
file operands; repeated `-` continues the same stream without closing or rewinding
it. Use `./-` for a file literally named `-`. Cat uses `fread_some` for every input:
terminal and pipe data is copied as it becomes available, without waiting to fill
its transfer buffer. File operands and file-backed stdin keep bulk reads and
normal EOF. Terminal input remains raw and blocking, without a Ctrl+D EOF convention.
Ctrl+C cancels shell editing, not a running cat. No options or terminal line
discipline are added.

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
cat app://share/hello.txt | head -n 2
cat app://tcc.pxe | head -c 16 > home://prefix
head -c 0 home://copy.txt
```

Closing the reader early discards unread pipe bytes and makes further upstream
writes fail with EPIPE. The shell reports earlier-stage failures while retaining
the last stage's result. There are no multiple-file headers or additional head
options. Terminal input still has no EOF convention: line mode needs its requested
newlines or an input error, and byte mode needs its requested bytes or an error.

## Foreground pipelines

Connect two through eight external commands with `|`:

```text
cat app://share/hello.txt | cat > home://copy.txt
cat < home://copy.txt | cat | cat
cat missing 2> home://errors.txt | cat > home://empty.txt
```

Whitespace around `|` is optional. Quotes and escapes keep it literal. Every
stage needs a nonempty command name; leading, trailing or adjacent pipes are
errors. `||`, `|&`, background pipelines and builtin stages (`cd`, `exit`, `mount`,
`title`, `session`) are rejected before any file is opened. Redirections retain
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

Only the first stage, when its selected stdin is a console, receives named
terminal-input and keyboard grants. Other stages cannot bypass their stdin with
those grants. Separate terminal-output and display capabilities retain the
ordinary child policy. Programs explicitly using those capabilities can still
write to or draw on the terminal. Ordinary stages receive neither launcher nor
pipe-creation authority; shebang adaptation does not add authority.

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

There is no cancellation, job control or terminal EOF convention. Ctrl+C cannot
interrupt a running pipeline. A child that waits on terminal input or ignores
its pipe can keep the shell waiting even after its peers finish.

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
`fg`/`bg`, signal delivery or termination command. Ctrl+C still cancels only the
shell's current input line. Use programs with their own bounded exit policy.
Background launch also works in scripts: launch failure stops the script, while
successful launch lets it continue regardless of the child's eventual result.
Children may outlive the shell; leaving it does not stop them.

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
explicit launcher grant. Ordinary shell children do not receive the launcher,
so launching a shell script as an ordinary foreground command currently fails
its resource check. Boot explicitly grants the init interpreter launch authority;
the default init uses `session` to pass it to the configuration launcher, which
then delegates it to the interactive shell.

## Optional host mount

`mount [--optional] [--read-only | --read-write] host` uses init's scoped
`host_mount` resource to open the selected export and bind `host://` in this
shell. Access defaults to read-only; write grants authorize mutation attempts
but do not establish backend or host writability. It rejects an
existing binding. Optional mode skips only a missing resource; any actual mount
failure is an error, which stops a script. Ordinary shells receive the mounted
root rather than mount authority. See [host setup and lifetime](virtio-fs.md).

## Session handoff

`session program [arguments...]` launches a successor in the same space and on
its assigned CPU, then exits the calling shell successfully without waiting.
For example, an init script can finish with `session app://shell.pxe`. The
command also works interactively; failed launch returns to the prompt, while
script mode reports the script name/line and exits with failure as usual.
Program lookup and quoting use the ordinary command rules, including shebang
launch. The `session` word is removed from the child's arguments.

The successor receives copies of the usual terminal, memory, app/home and optional
host root and
working-directory grants, current working-path metadata and initial environment,
plus an explicit `launcher` resource with LAUNCH authority. Other startup
resources, including the caller's `script` and `host_mount`, are not forwarded. Launching another
script supplies that target's own READ script grant through `program_launch`.
Ordinary foreground commands still receive no launcher.

Successful launch ends script execution immediately: later lines do not run,
and the caller never reads terminal input again. Closing its process observer
and exiting releases only the caller's references; the successor keeps its own
references and can read input and launch programs after the caller is reclaimed.
There is no wait for application readiness: success means the launch was
accepted, not that the new program will initialize successfully. A later exit or
fault does not bring back the original shell or restart the session.

This is explicit delegation followed by caller exit, not process replacement
or a terminal ownership protocol. It does not add supervision or `exec`.

## Startup and child authority

The shell expects named `input`, `output`, `memory` and `launcher` resources,
plus `app` and `home` roots. Input/output are separate console READ/WRITE grants;
memory permits MANAGE and launcher permits LAUNCH. Normally app supplies LOOKUP,
ENUMERATE and READ_FILES, and home additionally supplies CREATE, WRITE_FILES and
REMOVE. Directory grants can be restricted by the launcher; the shell queries
and preserves their actual rights.
An optional `display` resource supplies DRAW authority for the space. An optional
`clock` resource supplies READ and SLEEP authority for monotonic time. An optional
`keyboard` resource supplies INPUT authority for physical-key sessions.
The optional `host` root retains the access granted by init. Root names do not
determine permissions.

An initial directory chain is copied from startup, preserving its navigation
boundary. A supplied chain requires a descriptive working path beginning with
`app://`, `home://` or `host://` for prompt display. The path is not resolved to
replace the chain: actual handles remain authoritative, and insufficient grants
fail normally. With no initial chain the shell starts at `home://`. Explicit
scheme changes use the bound root's actual grant; each descendant lookup retains
its parent's grant. Crossing a retained ancestor boundary fails as in the native
path API.

Each child receives independent standard-stream grants selected from the shell's
bindings, explicit redirects and pipe connections, with their declared
console/file/pipe protocols. Background children omit stdin.
Missing streams remain absent; stderr never falls back to stdout or the terminal.
The script interpreter and session handoff preserve these bindings too.

Each foreground child receives explicit copies of terminal output, memory and
available roots with their actual grants, and the current directory chain
preserving each
handle's rights independently. Terminal input and keyboard are withheld for
file/pipe stdin and downstream pipeline stages as described above. It does not
receive the shell's launcher. When available, the [display](graphics.md),
[clock](timekeeping.md), [random](randomness.md) and [keyboard](keyboard.md) grants are also forwarded
to eligible foreground children and session successors; background children omit keyboard input.
The immutable initial environment is forwarded in full using
libpyxis's borrowed environment-array accessors. No environment mutation or PWD
maintenance is implemented. Children receive the full current working-path
display string alongside their directory handles. Display normalization removes
redundant separators and dot components, but lookup still walks the original
input: `missing/..` fails rather than skipping the missing directory.

The shell never reads terminal input while waiting. Successful wait means child
resources have been reclaimed; it closes the observers, reports nonzero exits or
faults, and prompts again after every foreground child has completed. The terminal advances to a fresh line
only when its cursor is not already at column zero, preserving unterminated child
output without inserting an extra blank line after newline-terminated output.
Failed launch returns to the prompt; failed wait ends the shell because
input ownership can no longer be assumed. Fatal user faults are reported through
the existing completion kind, with details left in the kernel log. No process
cancellation or terminal ownership mechanism is added.
