# Foreground shell

`make -C userspace shell` builds `shell.pxe`. Normal boot starts one shell on
CPU 1 when available, otherwise on the BSP, with `home://` as its working
directory. On multicore boots use Alt+Right to select CPU 1 before typing.
The normal initrd contains shell, ls, cat, mkdir and `share/hello.txt`; home is
initially empty and its RAM contents disappear on reboot.

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

On exit, process cleanup logs completion in Caelum and releases the shell's
resources. Its space, tab, framebuffer contents and shared namespace roots
remain alive. There is no automatic restart or new input consumer in that
space, but tab switching and kernel presentation continue. In the single-CPU
fallback, Caelum logs and shell output share a TTY and can disrupt line editing.

## Commands and quoting

The shell uses libterm's line editor and waits for one foreground child at a
time. The prompt is `> `. Whitespace separates arguments. Single and double
quotes preserve whitespace and allow empty arguments; adjacent quoted/unquoted
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

There is no expansion, substitution, globbing, scripting, comment syntax,
piping, redirection or background execution. Characters such as `$`, `*`, `#`,
`;`, `|` and `>` are literal argument bytes, not operators.

`cd path` changes the shell's owned directory chain. It requires one argument;
failure preserves the old chain and its rights. `exit` takes no arguments and
ends the shell successfully. Command failures do not accumulate into the shell's
exit status; unrecoverable terminal, wait or cleanup failures terminate it with
failure.

A command without `/` is a bare name: `cat` opens `app://cat.pxe`. There is no
PATH search or fallback. Names already ending in `.pxe` still receive the suffix
when bare; use `app://cat.pxe` or `./cat.pxe` to name an image directly. Paths
containing `/` are resolved as written. Builtins are recognized after quote
removal. An empty command name is an error.

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

Each child receives explicit copies of terminal input/output, memory, both roots
with the rights above, and the current directory chain. It does not receive the
shell's launcher. The immutable initial environment is forwarded in full using
libpyxis's borrowed environment-array accessors. No environment mutation or PWD
maintenance is implemented. Children receive no working-path display string,
since the shell does not maintain one after cd; their directory handles still
support relative access and parent navigation.

The shell never reads terminal input while waiting. Successful wait means child
resources have been reclaimed; it then closes the process observer, reports a
nonzero exit or fault, and prompts again. A newline after child completion keeps
unterminated child output separate from the line editor's next cleared prompt
row. Failed launch returns to the prompt; failed wait ends the shell because
input ownership can no longer be assumed. Fatal user faults are reported through
the existing completion kind, with details left in the kernel log. No process
cancellation or terminal ownership mechanism is added.
