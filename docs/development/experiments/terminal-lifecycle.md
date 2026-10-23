# Terminal adapters and foreground lifetime

2026-10-23, nested Q35/KVM, host CPU, four vCPUs, 4 GiB, OVMF, modern VirtIO
network/RNG and a private virtio-fs export. No native/PXE run. Implementation
image: Pyxis `8046091d`, userland `1ed8bf55`, ports `a16eecb8`; unchanged
kernel/SDK bundles from the raw API image were packed and verified locally.
Builder: `pyxis-llvm23.1.3-3d69e83`. Ordinary full image/client builds passed;
upstream Neovim warnings remain, with no new adapter build warnings.

## Observed behavior

- The ordinary libuv relay, with its existing tty input switched to RAW, received
  `03 04 1b` while its child and timers ran, then closed normally. No second
  native PASSTHROUGH owner is retained by libuv.
- A C RESIZED wait woke in plain interactive Remote: 100×30 → 120×36, generation
  1 → 2. Remote mux passed the same check, with application rows 28 → 34 after
  its title/status rows. Host PTY resize used the existing SIGWINCH client path;
  target delivery used native SIZE/RESIZED, with no target signal.
- The Go caller also observed positive READABLE for the remaining queued bytes
  after a one-byte read, then positive RESIZED and SIZE 120×36/generation 2
  through plain Remote. This was a bounded manual handshake, not a busy loop or
  a claim that Go has a blocking resize-subscription API.
- An ordinary full-screen caller left alternate-screen/region/style/cursor
  changes for its supervisor. Normal exit and user fault produced normalization
  before command completion. Local framebuffer captures additionally confirmed
  normal exit restored the primary screen and visible prompt cursor.
- Plain output redirection, `>file 2>&1`, `2>&1 >file` and a terminal-using final
  pipeline stage passed. Host-side inspection of the resulting files found only
  ASCII application cues and no escape/reset bytes; normalization went to the
  separately held console.
- A direct parent exited, or faulted, while its child retained raw input/output.
  The child kept writing without normalization or early command completion.
  After explicit child exit, normalization occurred and the original parent's
  actual exited/faulted result was preserved.
- QEMU GDB at sealing observed one remaining member, one attributed cleanup,
  `complete=0`, raw count 1 and interrupt arming 1. At the first normalization
  entry: members/children/launches/cleanup were all zero, `complete=1`, raw count
  0 and arming still 1. Thus group retirement and raw cleanup preceded the
  supervisor's reset and later interrupt disarming. Hardware breakpoints only;
  no injected target calls or memory mutation.

## Matched ordinary launches

Before: raw API image (`2816ff03` code inputs), userland `842225dd`, ports
`6c30faf8`. After: the implementation image above; identical kernel/SDK content.
Each existing machine client supplied 1,024 `echo -n` command lines and required
1,024 successful command completions plus a complete final drain. Host wall
seconds (includes TCP/terminal records, not pure target launch time):

| Before | After |
| --- | --- |
| 2.611 / 2.681 / 3.272 | 11.750 / 3.775 / 8.632 |

The added path creates/holds a group and terminal output, seals/waits for
retirement and closes those handles. Normalization also adds terminal output
records on every interactive command. Those are real additional operations;
these shared-host nested samples cannot isolate their costs or establish native
latency. No owned build/debugger ran during the samples; other host load was not
isolated. Ordinary Lua job-loop measurements do not isolate this change, since
they bypass the interactive shell's per-command scope. Scripts retain their
existing outer ownership rather than normalizing each internal command.

## Remaining qualification

Foreground cancellation policy is pending chronium's choice; current draft
inherits existing owned-group immediate stopping. Pane close, Remote disconnect,
one-CPU lifecycle and native use remain unclaimed.
Machine-mode geometry remains fixed by design. Go's nonblocking observation is
not a blocking/cancellable resize subscription. Raw logs, private ordinary caller
sources and framebuffer captures stay under `/shared/present/terminal-ui/`,
`terminal-ui-lifecycle/` and `terminal-ui-libuv/`.
