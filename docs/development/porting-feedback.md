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

- [Unbuffered line input](../technical-debt.md#unbuffered-line-input) has a
  measured cost in uniq. Address it in libc with an explicit descriptor/stream
  contract, rather than changing each line-oriented port.
- Sticky-error handling in fgets, deliberately outside the uniq task, is fixed
  by task 1 of [libc input read-ahead](../wip/stdio-input-buffering.md).
- [Duplicated port output lists](../technical-debt.md#duplicated-port-output-lists)
  can drift when a recipe gains another executable.
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

Claude reported two nonblocking limitations during sha256sum validation:

- Machine-mode output includes the shell's input echo and per-character line
  redraws, requiring cleanup before comparing program output. Consider an
  explicit session option that suppresses shell input echo/redraws at the source.
  Do not strip arbitrary terminal sequences from application output: those bytes
  may be legitimate output. The option's scope and negotiation remain to be
  designed; machine mode currently preserves the terminal byte stream.
- `command_complete` reports shell success/failure, not the child's numeric
  exit code, as the [remote contract](../userland/remote-terminal.md#client-modes)
  already documents. A future completion result should distinguish normal exit
  with its exact code, launch failure, and fault/termination. Preserve the shell's
  pipeline-status policy and the distinction between background launch completion
  and child exit; individual pipeline-stage results need not be added together.

Revisit these as a bounded remote-tool improvement when validating consumers with
distinct nonzero outcomes, such as grep. Until exact codes are observed separately,
reports based only on `command_complete` establish matching success/failure, not
identical numeric exit statuses.

Audit of the uniq and sha256sum validation: rerunning the unpatched host
references with raw codes gave only 0 and 1, with 1 for exactly the failure cases
(5 uniq, 6 sha256sum). Every guest failure case shows the shell's `Exited with
status 1` diagnostic, which carries the child's numeric code, so failures matched
exactly. Guest successes were observed as `command_complete` status 0 with no
diagnostic. Userland `3b9ba3f` `shell/launch.c` prints a diagnostic for every
stage that faults, is terminated or exits nonzero, and takes the command result
from the last stage, so that combination means every stage exited 0. This is
source-confirmed; the codes were not captured in a typed event. The [uniq reference](../userland/uniq.md#validation-evidence) now states
this; the merged sha256sum PR descriptions (ports #31, Pyxis #273) say "exit
statuses matched" and should be read with the same qualification. Parsing shell
diagnostics is exactly what a typed exit-code result would replace.

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
