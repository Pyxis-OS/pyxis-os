# Script launch

`program_launch()` in libpyxis accepts an opened image and the same explicit
request as `launcher_launch()`. Native PXE requests pass through unchanged.
The kernel launcher and PXE loader continue to load only native executables.

For a shebang script, the helper resolves the interpreter URI through the
caller's startup roots by default and submits that executable instead. The helper
accepts a borrowed interpreter path context; the shell supplies its current
namespace for ambiguity checking while retaining the startup-root behavior.
Interpreter lookup authority is separate from the namespace delegated to the
child, which can be absent for provider publication launches. It appends a READ
resource named `script` for the original file. Existing grant indices and child
resources, roots, working directory and environment retain their meaning; no
launcher authority is added. A caller-supplied `script` resource conflicts with
this convention and is rejected for shebang launch.

Interpreter arguments are its URI, the original script name from `argv[0]`,
then the remaining original arguments. The script name is descriptive, not
permission to reopen the file. The interpreter reads the granted file from
byte offset zero. Terminal input remains a separate resource.

The shared [parser contract](../../include/pxe/shebang.h) bounds the first line,
accepts LF or CRLF (or EOF without a newline), and permits spaces/tabs between
`#!` and one explicit `scheme://file` URI. It does not support interpreter
arguments, quoting, trailing whitespace or recursive script interpreters.
An invalid interpreter image is rejected by the native loader.

The helper borrows the request, closes temporary interpreter/directory handles,
and preserves all source grants on success or failure. Successful launch returns
an owned process observer; failure returns no child. Prefixes and temporary
arrays use userspace heap storage. Reads do not create a snapshot of a writable
script: the interpreter receives the same file object, which other authorized
writers may still change.

Boot shares the parser, but its interpreter lookup is deliberately limited to
`app://` followed by an exact initrd archive entry name. It neither walks general
kernel paths nor normalizes archive names. Boot installs the same script grant
and argument convention with its explicit initial-process resources.

Normal boot selects [init](init.md), whose default script hands off to the
interactive shell. The shell supports [script execution](shell.md#script-mode)
when explicitly granted the required resources. Its
[session command](shell.md#session-handoff) delegates launch authority and exits
after starting a successor.
