# Selected init and primitive shell scripts

Status: working milestone draft, not an implementation assignment. Agreed
direction is distinguished from proposals and open decisions below. See the
[planning index](boot-sdk-ports.md) for sequencing.

Agreed role: init performs setup and hands off to an interactive shell.
Supervision and restart policies are deferred until the website-server milestone.
The exact handoff mechanism remains open.

Proposed scope: replace the current application-space entry on CPU 1 (BSP on a
single-CPU boot), retaining Caelum's separate role. This does not define a global
Unix PID 1, complete supervision, or the future per-space lifecycle model.

Keep boot material in the initrd even after virtio-fs exists: init and its
interpreter must be readable before init can mount anything. The selected entry
must be explicit, with ordinary failure diagnostics and no silent fallback to
another executable.

A possible build interface is `INIT=<host path>`: stage those bytes under a
stable archive entry, without editing tracked source. Distinguish the build-host
path from a guest capability path. Exact Make names, a default script, optional
arguments and placement in the userspace repository are still open. Changing
an override must invalidate the relevant build outputs, even if the new file
has an older timestamp. A temporary init must not leak into subsequent default
builds or published artifacts.

Shebang handling should be a program-launch facility reusable beyond the
interactive shell. Interpreter lookup must stay within explicitly supplied
roots; knowing an interpreter URI does not grant access. Avoid growing a general
path parser inside the kernel. The boot adaptation and userspace launch helper
need a concrete design because today's loader accepts an opened image handle.

Decide the initial shebang grammar: explicit interpreter URI, whether an optional
argument is accepted, first-line length bound, CRLF handling and recursion/cycle
policy. Prefer a bounded, nonrecursive first implementation. An interpreter must
receive authority to read the script, not merely a pathname. Passing the already
opened READ capability avoids requiring a second lookup; argv convention and the
resource name need agreement.

Proposed shell script mode reuses current quoting and command execution: one
command per line, blank lines and whole-line comments, cd and foreground launch.
No expansion, variables, conditions, loops, pipelines or redirection initially.
Process the final line without a newline and report script name/line on errors.
Do not silently truncate long lines. Proposed boot-script policy is to stop on
syntax, cd, launch, nonzero child exit or child fault; confirm this rather than
implicitly adopting interactive-shell behavior. Script EOF exits without an
interactive prompt unless a handoff is explicitly requested.

Init needs setup authority that ordinary applications should not inherit.
Current shell children intentionally receive no launcher, so simply putting
`shell` in a script cannot launch a fully functioning interactive shell today.
Choose an explicit handoff/delegation mechanism, including eventual mount
management authority, before implementing startup scripts. Decide whether the
handoff launches a shell and lets init exit or turns init into the session
program, and what remains visible after startup failure or normal init exit.
Neither choice introduces a resident supervisor in this milestone.

## Completion boundary

Boot either a selected native init or a shebang init, run a short setup script,
and reach a usable interactive shell. A Make override can select a temporary
init without contaminating subsequent default builds. Validate with ordinary
builds and interactive boots.

No supervision, restart policy, mount implementation, shell control-flow
language, pipelines or redirection belongs in this milestone.

## Decisions before implementation

- Select the shebang grammar, script grant and argv convention.
- Agree on script failure behavior and the explicit shell handoff/delegation.
- Settle the Make override and default init placement.

Split the resulting design into focused implementation PRs after those decisions.
The SDK/repository split is not a prerequisite.
