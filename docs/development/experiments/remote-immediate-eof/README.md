# Remote input EOF qualification

2026-10-09, four-CPU nested QEMU. The owner assigned the failure recorded in
[#617](https://git.internal/PyxisOS/pyxis-os/pulls/617): immediate host stdin EOF
returned CALL_ENDPOINT_CLOSED before any command completed, while holding stdin
open through the documented FIFO mode worked.

## Reproduction and ownership

Fresh main `cd8aac31` reproduced with the stock host client and unchanged
userland `b8d542e4`, ports `0f0aa443`, fs `b427df29` and lwIP `a1aadb91`:

```sh
make -C tools remote
printf 'echo eof-one\necho eof-two\n' | build/tools/pyxis-remote \
  --machine --no-shell-echo --columns 80 --rows 24 127.0.0.1 12323
```

The host exited 1. JSON contained READY, the shell diagnostic
`shell: terminal: Native call failed (status 8)`, and FINAL with shell exit 1
and complete output draining. No command completion arrived. A held FIFO
completed two separately submitted echoes and `exit`, with numbered completions
1–3 and FINAL exit 0.

The owning layer is the kernel's terminal/clipboard receiver lifetime handling.
END_INPUT incorrectly invalidated an existing receiver as though the terminal
hung up; receiver reads then failed before draining input. Registration also
refused an EOF-closed input with queued bytes. Stock libterm uses the receiver
for ordinary line editing, so either arrival order could fail the shell.

Source history identifies kernel `08ee2fbe` and userland `1dffbccd`, integrated
by parent pin `550c8d45`, as introducing these paths. This is source/history
evidence, not a runtime bisect. Ctrl+C scanning and frame handoff are not the
failing paths. The host already sends INPUT before END_INPUT correctly.

The fix restores the existing [EOF contract](../../../userland/terminal-sessions.md#eof-and-hangup):
allow receiver registration and reads after END_INPUT, retain buffered input,
and wake blocking receiver reads without invalidating their epoch. Hangup,
release and process-stop invalidation remain. The existing closed-input
admission check refuses new paste transactions. No new ABI, dependency pin,
EOF policy, diagnostics or compiler-container rebuild is needed.

## Matched runs

Baseline kernel `cd8aac31` was built and archived before code edits; the final
fix was checked at `52c1e046`. Both used the existing LLVM23.1.3/49e2c1a builder,
default info logging and configuration, and verified independent SDK/userland/
ports bundles. Those bundles came from `500854ce`, whose complete source tree
is identical to main `cd8aac31`, with the same pins. Kernel source was rebuilt
for each run. Initrd and generated boot configuration were byte-identical.

| Artifact | Baseline SHA-256 | Fixed SHA-256 |
| --- | --- | --- |
| Kernel ELF | `4c452ac07ae95b7e2159d7af6614fa169d9761fbcd7c396cc7d7a1c7276cda93` | `2e92425933161e4d949e4ed1a08e105dca3e021db59fe883bdddc96613f72798` |
| Shared initrd | `25788581e234025ae1d3b10e0f80df1f85a80a9e041abc72ab82b523b619b68a` | Same |

QEMU 10.2.2, Q35, nested KVM, `-cpu max`, four cores/one thread each, 2 GiB,
fresh OVMF variables, standard VGA with display disabled, relative PS/2,
VirtIO-SCSI CD, RNG and VirtIO network. Host `127.0.0.1:12323` forwarded to
guest `10.0.2.15:2323`; no disk, audio, USB or host filesystem. Each image got
a fresh boot. Host clients were driven manually; no new tests or boot/input
automation were added.

| Final-code check | Result |
| --- | --- |
| Two newline-terminated echoes, immediate EOF | Exact output in order, completions 1–2, FINAL exit 0 / complete; host exit 0 |
| `echo pipeline \| cat`, another echo, `exit`, immediate EOF | Exact output, completions 1–3, FINAL exit 0 / complete |
| `cat` followed by `raw-data` and immediate EOF | Raw reader drained `raw-data\n`, completed once, FINAL exit 0 / complete |
| Empty stdin | No command completion; successful EOF and FINAL exit 0 / complete |
| EOF while an idle registered reader was waiting | Read-only GDB confirmed a valid terminal receiver before closing the FIFO writer; FINAL exit 0 / complete |
| Documented read/write-held FIFO | Two echoes submitted after separate completions, then `exit`; completions 1–3 and FINAL exit 0 / complete |
| Actual interactive host client | Typing, Backspace correction and echoed command output worked; prompt Ctrl+C cancelled a line |
| Ctrl+C during foreground `cat` | Reported Process terminated; a following echo worked and ordinary `exit` returned host status 0 |

## Current-main integration

Main advanced during qualification. Integration kernel `3cfc617a` merges main
`11d35fa6`, retaining its published userland `0c690289` and ports `8bff5dcd`
pins; no dependency PR or independent gitlink change is added. The ordinary
default `make -j16 image` passed from source with the same builder. On a fresh
boot in the configuration above, immediate two-command EOF, raw `cat` data/EOF,
held FIFO commands/exit, interactive typing/Backspace, prompt cancellation,
foreground `cat` Ctrl+C, subsequent echo and ordinary exit all passed again.
Machine sessions returned FINAL exit 0 with complete draining. Integration
kernel SHA-256 is `e248f1a70ae41a526d1c55a17b2d45537708a54dca493c29c230895d22badad7`;
initrd SHA-256 is `008bb37dec586d98081f052fd55d1cd60da0a5be1531cce9f0a1150c00099e97`.

## Limits

The blocking receiver wait-slot notification, hung-up registration refusal,
new-paste refusal after EOF and existing admitted-paste framing/deadline were
source-reviewed. No direct no-clock/infinite receiver-read or active-paste-at-EOF
runtime fixture was added. Existing libterm behavior still discards an unfinished
line on EOF; this fix does not submit it. No physical-host result or performance
claim is made. Images/hashes stay in the ignored worktree build directories;
JSON and serial captures remain local. Task-owned clients, QEMU and GDB stopped.
