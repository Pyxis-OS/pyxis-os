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
- [Sticky-error handling in fgets](../technical-debt.md#fgets-final-line-after-an-earlier-error)
  needs a focused correctness fix; it was deliberately outside the uniq task.
- [Duplicated port output lists](../technical-debt.md#duplicated-port-output-lists)
  can drift when a recipe gains another executable.
- Reported setup friction, not reproduced for this note: `make run` requiring
  a compiler for an already-built image, and fj deriving an API URL containing
  SSH port 2222. Check the supported invocation and actual cause before changing
  either workflow.

## Process experiment and open suggestions

The agreed next experiment is a small sbase sha256sum port without a separate
milestone or probe PR. Record scope/probe findings and validation in the delivery
PRs, retain dependency merge order, and stop for decisions that expand scope.
Focused commits and review still apply. Assess the result before making this a
general workflow rule.

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
