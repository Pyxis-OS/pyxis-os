# Terminal adapters and foreground lifetime

2026-10-23, nested Q35/KVM, host CPU, one/four vCPUs, 4 GiB, OVMF,
modern VirtIO network/RNG and a private virtio-fs export. No native/PXE run.
Final target image: Pyxis `179ccbfe`, userland `e42fa1d5`, ports `1000a13a`;
SDK rebuilt from those sources, unchanged kernel bundle verified locally.
Host renderer fix: `a4c1ca18`. Builder: `pyxis-llvm23.1.3-3d69e83`.
Full image/client builds passed; upstream Neovim warnings remain. QMP and the
kernel boot log independently confirmed the one-CPU repeat.

## Qualification

- Ordinary C/Go raw callers received `03 04 1b` without Enter; libuv's existing
  relay with RAW input also passed while its child and timers ran. C bounded
  RESIZED and Go positive READABLE/RESIZED observations passed through plain
  Remote. Host PTY 100×30 → 120×36 advanced native generation 1 → 2; mux
  application geometry was 100×28 → 120×34. Machine geometry stays explicit.
  After rebasing Go onto its merged standard-library adapters, the rebuilt
  `7d938797` caller repeated raw/readiness/refusal checks on one/four CPUs and
  plain Remote resize on one CPU. Its terminal implementation files were
  unchanged by that rebase; no compiler-container rebuild was needed.
- On one/four CPUs: normal exit, user fault, no-receiver force stop, cooperative
  first press, second-press force and cooperative parent cleanup with a surviving
  child passed. Graceful interruption kept EXITED/status 0 and cancelled the
  remaining command list. Two presses in one injection also forced stop (four
  CPUs). PASSTHROUGH kept Ctrl+C as byte 3.
- A parent exited or faulted while its raw child continued writing: no premature
  reset/completion. On child exit the original parent result survived. GDB
  observed raw 1/member 1 before sealing, then raw 0 and members/children/launches/
  cleanup all 0, complete 1 at the first normalization entry.
- In the final cooperative case, GDB stopped at TERMINATE only after direct-stage
  retirement: one descendant and one attributed cleanup remained. Normalization
  began only at complete 1 with all counters zero. Arming remained held until
  afterward. Hardware breakpoints only; no injected target calls/memory writes.
- Plain redirection, `>file 2>&1`, `2>&1 >file` and a terminal-using final pipeline
  stage passed. Host-side files contained ASCII cues without reset/escape bytes;
  normalization used the separately held console.
- Busy remote-mux pane close returned kernel group storage counts 4 → 2 on both
  CPU configurations. Disconnect with a live raw descendant returned Remote to
  its zero-group baseline. Plain disconnect with a raw application returned
  2 → 0 (four CPUs). Local mux pane close returned its count 3 → 1; framebuffer
  captures confirmed the remaining pane/prompt remained usable.
- Neovim entered/exited through the new libuv owner. A following echo remained
  visible. Qualification found that Remote ignored ESC7/8, allowing the margin
  reset to home/erase ordinary output. The renderer now saves/restores position,
  style and pending wrap per application screen, including resize clamping.
  Two ordinary echo lines and post-Neovim output passed after the fix. No
  separate cell backing store was added; old application contents are not saved.

## Earlier matched ordinary launches

Before: raw API image (`2816ff03` code inputs), userland `842225dd`, ports
`6c30faf8`. After: initial ownership image `8046091d`, userland `1ed8bf55`, ports
`a16eecb8`; identical kernel/SDK content. Each existing machine client supplied
1,024 `echo -n` lines and required 1,024 successful completions plus complete
final drain. Host wall seconds, including TCP/terminal records:

| Before | After |
| --- | --- |
| 2.611 / 2.681 / 3.272 | 11.750 / 3.775 / 8.632 |

Groups add create/hold/seal/wait/close operations; unconditional interactive
normalization adds output records. These are real costs. Shared-host nested
variation prevents precise/native attribution, and this is not pure target
launch timing. No owned build/debugger overlapped samples; other host load was
not isolated. This earlier pair predates the cancellation/host-renderer fixes;
no timing claim is made for the final head. Lua run loops bypass this interactive
per-command scope; scripts retain their existing outer ownership. Revisit the
cost with matched native interactive/launch-heavy use.

## Limits

Native qualification remains the milestone's final gate. Go supplies size and
one nonblocking readiness observation, not a blocking/cancellable resize API;
external x/term users must select the ported source. Machine geometry is fixed
by design. Input-loss propagation remains source-inspected in the raw API record.
Raw captures, private callers, ELF/debugger scripts and framebuffer captures stay
under `/shared/present/terminal-ui/`, `terminal-ui-lifecycle/`, `terminal-ui-c/`,
`terminal-ui-go/` and `terminal-ui-libuv/`.
