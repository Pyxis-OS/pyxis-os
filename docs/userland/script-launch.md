# Script launch

`program_launch()` in libpyxis accepts an opened image and the same explicit
request as `launcher_launch()`. Native PXE requests pass through unchanged.
The kernel launcher and PXE loader continue to load only native executables.

For a shebang script, the helper resolves the interpreter URI through the
caller's startup environment by default and submits that executable instead. The
helper accepts an optional borrowed path context of held directory bindings; an
absent or empty context, which the shell passes, selects the startup environment.
Interpreter lookup authority is separate from the roots delegated to the
child, which can be absent for provider publication launches. It appends a READ
resource named `script` for the original file. Existing grant indices and child
resources, roots, working directory and environment retain their meaning; no
launcher authority is added. Interpreter preparation retains the parsed prefix
and selected native FILE for `program_launch_prepared` or the corresponding
batch entry point. The caller can bind a retained native interpreter with
`program_image_select_interpreter`; shell startup selects it beneath its held
root or from its own bundle. A missing/unusable selection refuses launch without
a mutable-name fallback. Selection preserves source lineage; script-resource
name collisions refuse before launch. The image selects
code; it grants no trust or new authority. There is no interpreter-extras API.

The shell's trusted chain uses its private `interpreter_root`. A rootless
bundled shell instead retains its own `app/bin/shell.pxe` at startup and privately
forwards that image to nested exact-shell scripts. The shell's common preparation
retains its owned launch-source inventory for exact held-shell execution in
foreground, pipeline, background and capture positions. Ordinary native profiles,
explicit grant lists and bundle declarations still select authority for other
launches. A non-shell caller cannot recover the session shell's sources by
starting an interpreter. Lua interpreter source inheritance remains a separate
follow-up; its existing explicit launch rules apply.

Available private provisioning authority gives an exact held-shell launch a
fresh confined subtree under the existing enclosing owner. A client work leaf
lacks the rights to provision. Definite failure cleans unused preparation, while
successful or unknown admission retains its tree for confirmed enclosing-owner
cleanup or reboot. A script process observer alone never authorizes deleting
that subtree. See [shell runtime grants](shell.md#script-runtime-grants).
**Only trusted roots extend the trusted interpreter chain.** A caller-supplied
`script` resource conflicts with shebang launch and is rejected.

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
`boot://` followed by an exact initrd archive entry name. It neither walks general
kernel paths nor normalizes archive names. Boot installs the same script grant
and argument convention with its explicit initial-process resources.

Normal boot selects [init](init.md), whose default script hands off to the
interactive shell. The shell supports [script execution](shell.md#script-mode)
when explicitly granted the required resources. Its
[session command](shell.md#session-handoff) delegates launch authority and exits
after starting a successor.
