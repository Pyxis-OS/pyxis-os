# Small-port workflow feedback

Recorded 2026-09-30 after Claude completed the sbase uniq milestone. This is
feedback from work on libc, ports, build tooling, documentation and remote
validation, not an assessment of kernel internals. Proposed follow-ups below
do not change [AGENTS.md](../../AGENTS.md) or authorize implementation.

## What worked

- Explicit descriptor/FILE ownership and header contracts made libc changes
  understandable without reconstructing the whole implementation.
- The small recipe runner and strict patch application kept the upstream port
  narrow. Uniq needed ordinary libc additions and no kernel changes.
- Remote machine-mode `command_complete` events and FINAL drain state allowed
  command validation without prompt scraping or repeated screenshots. Preserve
  these semantics as the remote terminal evolves.
- Bounded tasks, discussion before policy decisions, and explicit dependency
  merge order let a new thread recover the work from repository documentation.
  Honest limitations prevented successful-looking placeholder behavior.

## Technical follow-ups

- The line-input cost measured in uniq was addressed in libc by
  [input read-ahead](../userland/stdio.md#input-read-ahead), with an explicit
  descriptor/stream contract rather than changes to each line-oriented port.
  [Console line input](../technical-debt.md#console-line-input) remains per byte.
- Sticky-error handling in fgets, deliberately outside the uniq task, was fixed
  in the same libc milestone.
- Port output lists could drift when a recipe gained another executable; the Make
  dependencies are now derived from each recipe's `metadata.lua` outputs.
- Setup friction observed during the uniq work, with the actual errors:
  - `make run` with a current `build/pyxis.iso` but no target compiler on PATH
    failed in `check-toolchain` with `Missing x86_64-unknown-pyxis-gcc: add the
    cross-toolchain to PATH or set CROSS_COMPILE.` (`Makefile:140`). Decide
    whether running an existing image should require the compiler.
  - With the remote `ssh://git@git.internal:2222/...`, fj 0.6.0 contacted
    `https://git.internal:2222/api/v1/user` and failed with `received corrupt
    message of type InvalidContentType`. The first login was stored under host
    `git.internal:2222`, and `fj -H git.internal whoami` then reported
    `unauthorized: token is required`. Logging in with `fj -H git.internal auth
    login` and passing `-H git.internal` worked. Whether host derivation from an
    SSH remote with a port is intended fj behavior was not established; document
    the working invocation before changing any workflow.

## Remote validation follow-up

Claude reported two nonblocking limitations during sha256sum validation. Both
are now implemented, as recorded in the
[remote contract](../userland/remote-terminal.md#client-modes):

- Implemented: `pyxis-remote --machine --no-shell-echo` negotiates quiet
  root-shell input in HELLO. Captured output then contains program output and
  shell diagnostics without the prompt, echo or line redraws. Application bytes
  are not filtered. Measured sessions showed quiet output as a byte-exact suffix
  of the default output.
- Implemented: `command_complete` carries a kind. `exited` reports the exact
  signed exit code of the last pipeline stage. `faulted`, `terminated`,
  `launch_failed`, `builtin` (0/1), `rejected` and `launched` distinguish the
  other outcomes. Background completion still describes launch, not later exit,
  and individual pipeline-stage results are still not reported.

Still open and outside that contract: per-stage pipeline results,
background-exit notification and quiet input for interactive clients. Revisit
them only when a consumer needs them.

Audit of the uniq and sha256sum validation, which predates typed completion:
rerunning the unpatched host references with raw codes gave only 0 and 1, with 1 for exactly the failure cases
(5 uniq, 6 sha256sum). Every guest failure case shows the shell's `Exited with
status 1` diagnostic, which carries the child's numeric code, so failures matched
exactly. Guest successes were observed as `command_complete` status 0 with no
diagnostic. Userland `3b9ba3f` `shell/launch.c` prints a diagnostic for every
stage that faults, is terminated or exits nonzero, and takes the command result
from the last stage, so that combination means every stage exited 0. This is
source-confirmed; the codes were not captured in a typed event. The [uniq reference](../userland/uniq.md#validation-evidence) now states
this; the merged sha256sum PR descriptions (ports #31, Pyxis #273) say "exit
statuses matched" and should be read with the same qualification. Parsing shell
diagnostics is what the typed exit-code result now replaces.

## Process experiment and open suggestions

The sbase sha256sum experiment used no separate milestone or probe PR: scope,
probe findings and validation accompanied the delivery PRs (ports #31 and
Pyxis #273). Dependency merge order, focused commits, review and discussion of
scope-expanding decisions still applied. Both PRs are now merged; assess the
result before making this a general workflow rule.

Other suggestions remain open: make model-specific delegation guidance usable
by other harnesses; reduce repeated contract prose; and discuss whether narrowly
authorized libc checks would be useful. Ordinary consumer validation did not
exercise all getline allocation/overflow failure paths. The current prohibition
on adding tests remains in force unless explicitly changed or excepted.

Claude initially found the PR/documentation overhead high for a small port, then
qualified that criticism after learning the project was eighteen days old. Both
observations matter: repository rules preserve coherence and enable handoffs,
while repeated paperwork can still be reduced. The goal is to preserve decisions
and evidence without requiring a separate planning deliverable for every small
change.
