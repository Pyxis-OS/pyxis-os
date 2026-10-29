# Native raw terminal qualification

2026-10-23, nested KVM on the Codex VM. No native hardware result.

The matched source pair was Pyxis `59e8d64d`, ports `22c4afb6`, userland
`96d2430b` versus the same tree with the libc raw adapter (`c45d17c`, subsequently
rebased without changing that patch). Q35, host CPU, four vCPUs, 4 GiB, OVMF,
modern VirtIO network/RNG and a private virtio-fs export; pinned builder
`pyxis-llvm23.1.3-3d69e83`. Ordinary full image builds passed. Qualification was
repeated after adopting the current dependency bases; these are runtime checks,
not another matched performance pair.

## Existing workloads

`session bin://ipcbench.pxe call|send --size 64|4096 --messages 256 --rounds 100`:
all passes verified. Completion milliseconds, median (range):

| Workload | Before | Raw adapter |
| --- | --- | --- |
| CALL 64 | 9.734 (1.179–27.533) | 10.697 (1.603–13.717) |
| CALL 4096 | 9.705 (1.434–135.090) | 3.998 (1.319–6.529) |
| SEND 64 | 2.167 (0.841–39.491) | 0.959 (0.598–1.569) |
| SEND 4096 | 1.127 (0.672–2.552) | 1.197 (0.665–4.166) |

SEND admission medians were 0.130/0.079 ms (64 B) and 0.165/0.172 ms (4096 B).
The ordinary Lua loop launching `echo -n` 1,024 times also completed before and
after. Shared-host load was not isolated; earlier launch captures overlapped
an image build and are functional evidence only. These highly variable nested
samples establish no attributable speedup or regression. FILE/PIPE close and
the IPC payload path acquire no new terminal lock; mode synchronization applies
to CONSOLE. Foreground subgroup changes require their own comparison.

## Terminal callers and debugger

Ordinary C and Go callers passed on one and four vCPUs, using the actual supplied
terminal. Ctrl+C, Ctrl+D and Escape arrived as `03 04 1b` without Enter or echo.
Checks covered nested/repeated/out-of-order restores, output-only raw refusal,
fd replacement/stale state refusal, non-consuming readiness and pipe rejection
without ambient-terminal fallback. Go used fork `674c615e`; C used the SDK raw
adapter. The cmd-vendored x/term source was selected explicitly for that caller.

After Go #7 merged, the unchanged terminal implementation was rebased onto
`11ea82ca` as `7d938797`. Target packages and the ordinary caller were rebuilt.
Raw/pipe checks and positive READABLE passed again on one/four CPUs; positive
RESIZED with generation 1 → 2 passed through plain Remote on one CPU. No kernel
or ABI change was needed for this dependency refresh.

A remote mux terminal changed from 100×28 to 120×34 application cells when its
host PTY changed from 100×30 to 120×36. The C bounded RESIZED wait woke and SIZE
advanced from generation 1 to 2. Same-generation polling returned zero events.
This checks the existing mux path, not yet plain Remote resize.

QEMU GDB inspected the C final-close path: references were zero, storage seven
and the raw hold nonzero at final alias release. After close, five snapshot
storage references remained, `closed=1` and the private raw slot was zero; the
caller then rejected stale Restore and freed the snapshots. No target calls or
memory mutation were injected. An earlier attempt to locate state through a
user ELF global read the wrong address; only the corrected function-argument
inspection is evidence.

Raw logs and ordinary caller sources remain under
`/shared/present/terminal-ui/`, `terminal-ui-c/` and `terminal-ui-go/`.
Input loss/hangup propagation is source-inspected here; fault, forced-stop,
descendant, pane-close and Remote-disconnect restoration belong to the next
integration gate. No native terminal or crash-restoration result is claimed.

## Raw termios and attachment drain, 2026-10-27

The ordinary full image passed at Pyxis
`64f611662ecd6c1e811d9a01ecb6b9b411eecfd7`, userland
`1a6d2f9328fc03b125c29be09b356be501a96c62`, ports
`54c6e591846f88570605cc348151b7de2a92b36a`, existing LLVM
`8a4d001d54806d48dc599ec9e658ba6113567c51`. Later reporting edits change no
target code. Q35/KVM, host CPU, four vCPUs, 4 GiB, VirtIO GPU/network, HDA null
sink and virtio-fs; normal RAM-live PXE command line. No native hardware result.

One boot exercised the then-editor's edit/save/reopen and normal exit directly
and through the Multiplexer space. Raw setup/restoration retained upstream
TCSAFLUSH.
GDB observed session drain with a 30,000 ms budget, one DRAIN authority and
PASSTHROUGH changing from zero before setup to one before restoration. The
attachment had already consumed the captured prefixes (17,955 and 18,589
record bytes), so this checks the completed-fence path, not a forced blocked
wait, detach/timeout case or failure matrix. No target data was mutated by
the debugger. The four timing cases and snapshot/close contracts were reviewed
in source; this editor run uses MIN=0/TIME=1.

Existing `top -b -d 1 -n 6 -p PID` recorded five interval samples after dropping
the first cumulative sample, with no debugger during measurement. Baseline
Pyxis `a497e825` is source-tree-identical to merged main `aa03ef2e`; userland
`4269b599`, ports `9fb1a085`, same device configuration and two-line file.

| Whole-QEMU CPU, percent of one host CPU | Baseline mean (range) | After mean (range) |
| --- | --- | --- |
| Then-editor idle | 6.6 (6–8) | 3.2 (2–4) |
| Shell idle | 6.2 (6–7) | 3.2 (3–4) |

These include all guest services and uncontrolled concurrent nested-host load;
the lower whole-VM values establish no attributable editor speedup or regression.
The matched manual edit/save/reopen sequence passed. All owned runtime and
build processes were stopped. A first build on the shared export failed with
ENFILE; a fresh local-disk build eliminated that environmental failure.
