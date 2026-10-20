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
launcher authority is added. A caller may offer extra named grants for one exact
parsed interpreter URI (`program_launch_extras`). ROOT selection opens that
interpreter beneath a held directory; IMAGE selection takes a READ-only copy
of a held native FILE. A selected missing/unusable image refuses launch without
a mutable-name fallback. Both preserve source lineage, reject conflicting entry
names and add nothing to native executables or other interpreters. The image
selects code; it grants no trust or new authority.

The shell's trusted chain uses its private `interpreter_root`. A rootless
bundled shell instead retains its own `app/bin/shell.pxe` at startup and privately
forwards that image to nested exact-shell scripts, together with only its held
pipe CREATE, subgroup launcher LAUNCH and optional threads MANAGE. An explicitly
supplied private provisioner and READ clock let the exact selected shell receive
a fresh private provisioning subtree. Native images and other interpreters
receive none, and a client work leaf lacks the rights to provision. Creation is
part of interpreter preparation; definite failure cleans it, while successful
or unknown admission retains it for enclosing-owner cleanup or reboot. A script
process observer alone never authorizes deleting that subtree.
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
