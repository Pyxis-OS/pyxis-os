# Selected init and primitive shell scripts

Status: agreed milestone scope, not an implementation assignment. Resolve the
remaining interface details before their implementation tasks. See the
[planning index](boot-sdk-ports.md) for sequencing.

## Boot and build contract

Init performs setup and explicitly hands off to an interactive shell. Replace
the current application-space entry on CPU 1 (BSP on a single-CPU boot), keeping
Caelum's separate role. This does not define a global Unix PID 1 or the future
per-space lifecycle model. Supervision/restart policy waits for the web server.

The default init is a small shell script in the userspace sources, exercising
shebang launch. Native executable init overrides are also supported. Init and
its interpreter remain in the initrd so neither depends on a future mount.

`make image INIT=/path/to/init` selects a host file to stage under a stable
archive entry. Without the override, restore the default init. Changing the
selected file must rebuild the relevant outputs even if its timestamp is older.
Do not leave temporary init contents in later default builds or artifacts.

## Shebang and script contract

Accept an explicit interpreter URI, such as `#!app://shell.pxe`, without optional
interpreter arguments. The interpreter must be a native executable: recursive
shebang interpretation is outside this milestone. Interpreter lookup uses
explicitly supplied roots; a URI does not grant authority.

Give the interpreter an already-open READ capability for the script and its name
for diagnostics. Reopening the script by pathname is unnecessary. Shebang launch
should be reusable beyond the interactive shell. Keep general path parsing out
of the kernel; the boot adapter and userspace launch helper need a concrete
integration design because today's loader accepts an opened image handle.

Script mode reuses current quoting and command execution: one command per line,
blank lines, whole-line `#` comments, `cd` and foreground programs. No variables,
expansion, conditions, loops, pipelines or redirection initially. Handle the final
line without a newline and reject oversized lines rather than truncating them.

Stop at the first syntax error, failed `cd`, failed launch, nonzero child exit or
child fault. Report the script name and line, leaving the diagnostic visible.
Do not silently fall back to an interactive shell. EOF exits unless the script
explicitly requests a session handoff.

## Session handoff

Provide an explicit command such as `session app://shell.pxe`. Launch the session
with its required capabilities, including launch authority, and let init exit
after successful launch. A failed handoff follows the script failure policy.
Ordinary commands retain their restricted grants; they do not gain the launcher
just because their parent is init. Future mount-management authority stays with
init unless explicitly delegated.

This is an initial handoff mechanism, not process replacement. A real `exec` is
wanted later, with its own resource and failure contract. This milestone does
not require it or introduce a resident supervisor.

## Focused implementation tasks

- [ ] Define the shared script-launch contract: bounded shebang parsing, script
  grant/name convention, interpreter lookup and boot adaptation.
- [ ] Add script mode using the existing parser and foreground execution, with
  line diagnostics and the agreed failure behavior.
- [ ] Add explicit session launch/delegation and verify that the shell remains
  usable after init exits, including input ownership and resource lifetime.
- [ ] Select native/script init at boot, ship the default script, and add the
  Make override with correct rebuild behavior. Build and boot both forms normally.

These are proposed PR boundaries; keep dependent changes together where needed
to preserve working builds. Before the first task, settle first-line bounds,
CRLF handling, the script resource/argv convention and the launch integration.
The SDK/repository split is not a prerequisite.

## Completion boundary

Boot a selected native or shebang init, run setup and reach a usable interactive
shell. A temporary init override does not contaminate the next default build.
No supervision, restart policy, mounts or broader shell language belongs here.
