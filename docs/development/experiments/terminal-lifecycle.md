# Terminal adapters and foreground lifetime

2026-10-23, nested Q35/KVM, host CPU, one/four vCPUs, 4 GiB, OVMF,
modern VirtIO network/RNG and a private virtio-fs export. Builder:
`pyxis-llvm23.1.3-3d69e83`. No native/PXE action or native timing claim.

Final code: Pyxis `aa10a24d` on main `0a3e0c03`, userland `c0a2a62`, ports
`e1d9093e`, Go `9bac7195`. Ordinary kernel/SDK/runtime/ports/userland and
`DISTRIBUTION=full` image builds passed. Existing upstream warnings remain.
No compiler-container rebuild was needed. The rebase preserved the terminal
patches and main's package-based program lookup. A final mechanical restack onto
main `f26b1b32` moved the libuv pin to `f409955` after unrelated graphics ports
merged. Kernel, public ABI, userland, host-client and libuv source paths are
identical to the measured revision; the recorded binary provenance stays above.

## Qualification

- Earlier C/Go raw-state, readiness/refusal, libuv relay/Neovim, resize,
  normal/fault/forced/cooperative completion, redirection, pane closure and
  Remote disconnect checks are recorded in [raw API qualification](terminal-raw.md)
  and the private captures. GDB proved actual subtree retirement before
  normalization: members/children/launches/cleanup zero, complete one. Group
  storage returned to baseline after pane close/disconnect. Redirected files
  contained no normalization controls.
- Final-base conditional checks repeated on one/four CPUs. GDB completion flags
  were zero for normal non-raw exits, including exit 7; normalization was not
  attempted. Raw acquisition followed by successful release returned raw-history
  flag one and normalized once. A raw child in a separate subgroup also propagated
  flag one to its non-raw parent. Direct/child faults and forced stop returned
  abnormal flag two and normalized once after retirement.
- Machine/no-echo and capture scopes retain groups but skip automatic
  normalization. Captured bytes and successful final drains remained correct.
  Interactive GDB confirmed inner capture output handle zero and the eligible
  outer scope's independently held terminal output.
- `reset` emitted the shared normalization sequence; `clear` emitted screen erase,
  supported-scrollback erase and cursor home. Both passed on one/four CPUs.
  Missing named output and extra arguments failed explicitly, without control
  writes or stdout fallback. Mux `clear` during history browsing erased retained
  rows: GDB observed history zero, scrolled-row identity 28, selection zero;
  subsequent output remained usable. The mux patch was unchanged by the rebase.
- Final ports close/replacement callers passed on one/four CPUs: explicit
  NORMAL/reset retained EBADF before close; after `uv_run` delivered the deferred
  close callback, snapshot/list were empty, reset succeeded, callback count was
  one and replacement I/O remained usable. EIO remains conservatively fatal
  because libc uses it for uncertain outcomes; no such outcome was injected.
- The host renderer preserves saved position/style/pending wrap, including resize
  clamping. Neovim exit followed by visible echo passed. There is no separate
  cell backing store or promise to recover prior application contents.

## Matched ordinary-command cost

Each existing machine/no-echo client supplied 1,024 `echo -n` commands and required
1,024 successful completions plus complete final drain. Four fresh boots used
baseline → final → final → baseline, three samples per boot. Same kernel, SDK,
daemons, image contents, client and QEMU configuration within each pair. Baseline
is a private measurement-only omission of the new ordinary foreground scope;
final is byte-identical to the submitted shell. It is never merged. No owned
build/debugger overlapped timing; shared-host load was not isolated.

Host wall seconds include TCP/session delivery, not isolated syscall time:

| Pair / variant | Six samples (s) | Median (range), s |
| --- | --- | --- |
| Final base `aa10a24d`, baseline | 1.729 / 1.755 / 1.710 / 1.943 / 1.953 / 2.522 | 1.849 (1.710–2.522) |
| Final base, final | 2.276 / 2.154 / 2.121 / 1.913 / 3.522 / 1.850 | 2.137 (1.850–3.522) |
| Before rebase `904db5ce`, baseline | 1.648 / 1.645 / 1.641 / 1.645 / 1.672 / 1.665 | 1.646 (1.641–1.672) |
| Before rebase, final | 1.824 / 1.859 / 1.829 / 1.851 / 1.871 / 1.868 | 1.855 (1.824–1.871) |

Final-base difference of medians: **282 µs/command (+15.6%)**; the quieter
pre-rebase pair gave **204 µs/command (+12.7%)**. All 24 samples passed and both
variants emitted zero normalization output records. Variation and the different
base prevent attributing the difference between pairs to code. Native cost is
unmeasured. Owned groups at about 0.2 ms/command were accepted
(chronium, 2026-10-23); keep their lifetime boundary.

The earlier submitted `f707c1a7` split estimated 203 µs/command for groups and
823 µs for unconditional normalization. Normal non-raw exits now skip the latter,
and excluded profiles use payload-free group waits. Ordinary interactive scopes
request sticky raw/abnormal flags at their existing completion wait; there is no
extra query or periodic poll. A normal non-raw program may leave presentation
dirty; `reset` covers this accepted gap, as documented in the shell reference.

## Limits and records

Native qualification remains milestone gate 5. Go provides size/nonblocking
observation, not a blocking/cancellable resize API; external x/term consumers
select the ported source. Machine geometry remains explicit. Input-loss
propagation is source-inspected in the raw API record.

Current runtime/debugger captures and matched samples stay in
`/shared/present/terminal-ui/selective/rebased-final/` and `rebased-cost/`.
Earlier runs remain in `selective/final/`, `selective/cost-final/`, `final-cost/`
and `lifecycle-final/`. Private callers/ELFs and renderer captures stay beside
those records or in `/shared/present/terminal-ui-{lifecycle,c,go,libuv}/`.
