# Foreground shell

`make userspace` builds the SDK, shell and core utilities. Normal boot runs the
default [init script](init.md), which hands off to one shell on CPU 1 when available,
otherwise on the BSP, with `home://` as its working directory. On multicore boots
use Alt+Right to select CPU 1 before typing. The normal initrd contains init,
shell, ls, cat, mkdir, [Kilo and its license](ports.md), and `share/hello.txt`;
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

There is no expansion, substitution, globbing, piping, redirection or background
execution. Characters such as `$`, `*`, `;`, `|` and `>` are literal argument
bytes, not operators. Interactive input treats `#` literally too; script mode
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
the default init uses `session` to pass it to the interactive shell.

## Session handoff

`session program [arguments...]` launches a successor in the same space and on
its assigned CPU, then exits the calling shell successfully without waiting.
For example, an init script can finish with `session app://shell.pxe`. The
command also works interactively; failed launch returns to the prompt, while
script mode reports the script name/line and exits with failure as usual.
Program lookup and quoting use the ordinary command rules, including shebang
launch. The `session` word is removed from the child's arguments.

The successor receives copies of the usual terminal, memory, app/home root and
working-directory grants, current working-path metadata and initial environment,
plus an explicit `launcher` resource with LAUNCH authority. Other startup
resources, including the caller's `script`, are not forwarded. Launching another
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
memory permits MANAGE and launcher permits LAUNCH. The app root supplies LOOKUP,
ENUMERATE and READ_FILES. Home additionally supplies CREATE and WRITE_FILES.
This first shell has explicit policies for these two namespaces.

An initial directory chain is copied from startup, preserving its navigation
boundary. A supplied chain requires a descriptive working path beginning with
`app://` or `home://` to select the requested rights. The path is not resolved to
replace the chain: actual handles remain authoritative, and insufficient grants
fail normally. With no initial chain the shell starts at `home://`. Explicit
scheme changes select that scheme's rights; relative changes keep the current
rights. Crossing a retained ancestor boundary fails as in the native path API.

Each foreground child receives explicit copies of terminal input/output, memory, both roots
with the rights above, and the current directory chain. It does not receive the
shell's launcher. The immutable initial environment is forwarded in full using
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
