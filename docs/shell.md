# Shell

`make userspace` builds the SDK, shell and core utilities. Normal boot runs the
default [init script](init.md) and [session launcher](session-configuration.md),
which by default start separate shells on CPUs 1 and 2 when present,
or one on the BSP for a single-CPU boot, with shared `home://` as their working directory. On multicore boots
use Super+Right to select CPU 1 before typing. The normal initrd contains init,
shell, ls, cat, mkdir, rm, rmdir, mv, [Kilo and its license](ports.md), and `share/hello.txt`;
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

The shell uses libterm's line editor and waits for one foreground child at a
time. The prompt shows the working path, for example `home://notes> `. Long
paths show an ellipsis and their tail, keeping at least half the first row for
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

There is no expansion, substitution, globbing, piping or redirection. Characters
such as `$`, `*`, `;`, `|` and `>` are literal argument bytes, not operators. Interactive input treats `#` literally too; script mode
supports whole-line comments.

`cd path` changes the shell's owned directory chain. It requires one argument;
failure preserves the old chain, its rights and the displayed path. `exit` takes
no arguments and ends the shell successfully. Interactive command failures do
not accumulate into the shell's exit status; unrecoverable terminal, wait or
cleanup failures terminate it with failure.

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

The first malformed command, failed `cd` or launch, nonzero child exit, child
fault or read failure stops the script with failure. Shell diagnostics include
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

Each foreground child receives explicit copies of terminal input/output, memory, available roots
with their actual grants, and the current directory chain preserving each
handle's rights independently. It does not receive the
shell's launcher. When available, the [display](graphics.md),
[clock](timekeeping.md), [random](randomness.md) and [keyboard](keyboard.md) grants are also forwarded
to foreground children and session successors; background children omit keyboard input.
The immutable initial environment is forwarded in full using
libpyxis's borrowed environment-array accessors. No environment mutation or PWD
maintenance is implemented. Children receive the full current working-path
display string alongside their directory handles. Display normalization removes
redundant separators and dot components, but lookup still walks the original
input: `missing/..` fails rather than skipping the missing directory.

The shell never reads terminal input while waiting. Successful wait means child
resources have been reclaimed; it then closes the process observer, reports a
nonzero exit or fault, and prompts again. The terminal advances to a fresh line
only when its cursor is not already at column zero, preserving unterminated child
output without inserting an extra blank line after newline-terminated output.
Failed launch returns to the prompt; failed wait ends the shell because
input ownership can no longer be assumed. Fatal user faults are reported through
the existing completion kind, with details left in the kernel log. No process
cancellation or terminal ownership mechanism is added.
