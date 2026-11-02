# Persistent libuv readiness qualification

2026-11-02, limits-audit WP5. [Contract](../../wip/libuv-readiness.md).
This is nested-QEMU evidence, with no native or injected-failure claim.

## Inputs

Before-code consumer: merged WP3 userland `a0a4a9d8`, ports `02aec1f2`, plus
WP6 libc and SDL stack changes (`3218c829` / `62d62445`). Fresh libuv 1.52.1,
Lua 5.1/luv and Neovim recipes were built before the backend implementation.
Candidate: userland `db20d9ff`, ports `dcaba778`, kernel/SDK source `7d197253`.
The later linear restack on main `e87d062b` changes package tooling/docs only;
its kernel, ABI and dependency pins are identical to that qualified source.

Both use the existing `8bdf136` cross compiler, hosted -O2/no-red-zone SDK flags,
Q35/KVM, host+invtsc, 8 GiB, OVMF, modern VirtIO network/RNG and a RAM archive.
No native LLVM prefix was substituted or rebuilt. Local sources, SDKs and images
stay under `/home/chronium/{src,build}`; small receipts and fixture sources are
under `/shared/present/limits-audit/wp5-evidence/`.

## Ordinary runtime checks

The baseline passes four simultaneous Neovim cat jobs (stdin/stdout/stderr plus
process observer). Requesting twenty admits seven before the old policy refuses;
all seven still produce their exact output, exit callback and EOF and are joined.
With the persistent backend, one- and four-CPU boots pass 4, 20 and 64 simultaneous
jobs: at least 256 child handles at 64 jobs, with exact labelled output, one exit
per child, both EOFs and confirmed cleanup. The existing relay also completes.
Commands use `nvim --headless -u NONE -i NONE -n --noplugin`, `jobstart`, `chansend`,
`chanclose` and `jobwait`; no replacement job or benchmark framework was added.

Further ordinary four-CPU clients pass:

- 48 observed launches in one set, exact keys/tags, actual process retirement,
  removal/cleanup and missing-CONTROL refusal.
- Eighty PIPE sources written before collection: every byte delivered, check
  phases between bounded batches, timer runs and every handle closes.
- Two loops keep callbacks separate; a read callback can close another already
  ready stream without dispatching its stale event.
- Twelve 32 KiB cat exchanges, stop/restart inside read callbacks, callback close,
  write/shutdown completions and coalesced async close.
- CUT of active and disarmed PIPE observations: active uv_run reports EACCES
  without invented EOF; disarmed state has no callback/error spin, and activation
  reports the retained denial. Subsequent cleanup and fresh Remote commands pass.
- Cooperative local signal delivery/close, and a 31,000 ms UV_RUN_ONCE timer
  fires once after 30,999 ms rather than returning at the native 30-second slice.
  This is deadline correctness, not a performance sample.

The Neovim negative-loop-error policy is implemented and source-inspected, not
fault-injected. Counter/generation exhaustion and unconfirmed-close paths are
also source-inspected only. No raw-console Ctrl+C, native capacity exhaustion,
concurrent alias race, exhaustive reclamation proof or native performance claim.
LuaLS uses its separate bee worker/channel backend; it is not a libuv consumer.
The reached libuv/Lua5.1/Neovim recipes and ordinary Lua prepared-launch caller
were rebuilt together. The selected rescue archive's iobench pipe child-image
lookup refused; no pipe-throughput result is claimed.

## Matched nested cost

The existing four-job Neovim fixture records uv.hrtime from immediately before
job creation through exact output/exit/EOF accounting and cleanup. Before/after
static binaries run in the same candidate-kernel boot with identical inputs.
This compares the backend and reached consumer code, including its observed-launch
route; it is not a separate kernel A/B. Builds finish before timing.

The first three pairs measured before 37.03 ms (29.56–60.58), after 46.14 ms
(39.76–84.57). Six follow-up pairs alternate order AB/BA:

| Four-job interval | Median ms | Range ms |
| --- | ---: | ---: |
| Before, six samples | 35.36 | 32.61–47.66 |
| After, six samples | 46.82 | 41.31–70.22 |

The +32% median is a real observed nested cost, not dismissed as noise. A separate
six-pair phase check using the same fixture's uv.hrtime identifies admission
(before the first jobwait) at 4.97 → 9.35 ms. The send phase is 8.29 → 8.86 ms;
release-to-drain is 21.98 → 25.53 ms with wide variation. Phase output itself is
excluded from each phase, so its total is not substituted for the uninstrumented
interval above. Medians of phases are not additive.

Source inspection identifies three native REGISTER calls per three-pipe child,
UPDATE when stream interest changes, joined REMOVE on close, and atomic process
watch admission at launch. The old count/transient path did not perform those
persistent controls. These enforce admission before ownership/publication and
withdrawal before callback storage is freed; unchanged-interest updates already
avoid a syscall. This explains added work, not an exact allocation of every
millisecond: kernel/worker phase attribution and native magnitude are unmeasured.
Native IPC is much cheaper than nested exits; do not extrapolate this percentage.
Measure the matched workload natively after the one coordinated compiler image
before treating the performance qualification as complete.
