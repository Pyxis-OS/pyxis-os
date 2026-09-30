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
identical numeric exit statuses. Audit the uniq/sha256sum validation wording on
that basis; this note does not claim their raw exit codes were independently
captured or invalidate their output comparisons.

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
