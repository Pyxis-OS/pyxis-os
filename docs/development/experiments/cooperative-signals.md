# Cooperative libc signals qualification

2026-10-28–29, nested Q35/KVM, host CPU, four vCPUs/4 GiB, VirtIO GPU/net,
HDA/null audio and writable virtio-fs. RAM-live archive with
`init=boot://boot-init.pxe nvme.write=0 display.timing=off log.udp=1`.
Existing LLVM 23.1.3 image `8bdf136`; no compiler rebuild or new shipped command.

## Revisions and commands

Clean-main baseline: parent `79ccea04`, userland `2101e916`, ports `18b56bf0`.
Consumer image: parent `61763796`, userland `22295c9e`, ports `64f3f914`;
ordinary full build and matching QEMU passed. Parent `f7e26daa` rebases those
inputs onto main `6f67ace2` through documentation changes only; CI run 4819
passed build, ABI, filesystem, pins and changes.

The owner-requested remaining runtime checks used a local qualification client
linked to userland `22295c9e` with SDK parent `f7e26daa`, running on the sealed
consumer image's unchanged native ABI. Its temporary bundle declares memory,
clock and threads; it is not a recognized interpreter and receives the normal
direct-stage Ctrl+C policy. No supervisor/interpreter policy was changed.

Run `host://signals-check.pxb CASE`, with CASE from the table below. Each live
case receives one manual Ctrl+C; `read` then receives `x`. Exact client source,
binary/SDK/image hashes, logs and screenshots stay in the handoff packet/PR.

## Observed results

All twelve cases passed: 134 logged assertions plus the observed exit status.

| Cases | Result |
| --- | --- |
| `local` | Coalesced local raise/pending; persistent signal(); action mask/errno restoration; automatic self-blocking; nested NODEFER and explicit self-mask precedence; RESETHAND visible before callback; all four accepted flags together; SI_USER/zero fields/NULL context; pending ppoll/pselect/sigsuspend; rejected inputs and unchanged prior action; transactional native receiver conflict/release; worker refusals and unchanged pending state. |
| `ppoll`, `pselect` | Native INT woke an enrolled temporary-mask wait: EINTR even with RESTART, input result sentinels preserved and original blocked mask restored. SI_KERNEL, zero other fields and initial-thread delivery. |
| `read-eintr`, `read` | Without RESTART: EINTR. With RESTART: callback returned, read stayed blocked, then returned the supplied byte. |
| `stdio`, `sleep`, `suspend` | Stdio ferror/EINTR without false EOF; sleep EINTR with actual remainder despite RESTART; suspend EINTR with its old blocked mask restored. |
| `pending`, `restore` | Masked native INT latched without a callback and ran on unmasking. A temporarily blocked event ran only after restoring the original mask, preserving the completed timeout result 0. |
| `raw`, `reset-int` | Raw Ctrl+C was byte 3 with no INT callback. After RESETHAND, the next local INT produced ordinary exit 130, observed by the shell. |

Fastfetch's upstream generator rendered, accepted selection/save, produced a
readable config and redrew after idle mux split/zoom. BusyBox less rendered a
held file, redrew on idle resize, and reached line 120/END in `cat FILE | less`
then quit. vi and Links rendered their held files and quit.

Four one-second idle-less host CPU samples were 4/4/3/4% before and 3/4/3/3%
after, discarding cumulative first samples and running no local build. Three
masked-pending samples were 3/4/3%. These short nested observations overlap and
show no idle hot loop; they are not a latency or native performance bound.
Unrelated Intel inventory changes also landed between the initial images.

## Limits

BusyBox less loops on backslashes if resized during an unfinished search prompt:
upstream getch_nowait sees winch_counter, but less_gets never reaches the outer
loop that clears it. Its upstream draft remains unfiled with chronium. Fastfetch
requests autowrap-disable (`CSI ? 7`), outside the terminal repertoire; long help
rows wrap in narrow panes. Neither was rewritten in the port.

Official Lua remains force-stopped by image-identity interpreter policy, even
when launched explicitly; its Ctrl+C run is not an INT handler qualification.
Worker checks establish rejection/local-pending preservation, not a separate
native-notification consumption experiment. Manual read-restart evidence is
not an instrumented wait-entry trace or an exhaustive interleaving proof.

You should know: native asynchronous signals, later TERM/HUP/CHLD/ALRM sources
and arbitrary-thread libuv delivery remain outside this slice. See the
[contract and rebuild needs](../../userland/cooperative-signals.md).

## Initial-thread loop consumers (2026-10-29)

Baseline parent `c65430c6`, userland `22295c9e`, ports `64f3f914`.
Qualified userland `bb25aeef`, ports `8b71e0c7`, same kernel sources; parent
integration changes documentation and pins only. Existing LLVM builder
`pyxis-llvm23.1.3-8bdf136`, `make -j16 image DISTRIBUTION=full`, nested
Q35/KVM host CPU, four vCPUs/4 GiB, VirtIO GPU/net and matching OVMF.
Local optical boot and plain remote terminals; no private assets or Go changes.

Neovim rendered and changed from 80×24 to 100×32; `:echo &columns &lines`
reported 100 32. Raw Ctrl+C interrupted `:sleep 100` without exiting. In
`nvim --headless -u NONE -c "sleep 100"`, Ctrl+C produced upstream
"Caught deadly signal 'SIGINT'", cleanup and ordinary exit status 1.
Two additional luv WINCH watchers each received one resize. A callback that
stopped/closed itself and its sibling produced counts 1/0, with clean exit.
GDB stopped Neovim's signal_watcher_cb under uv__pyxis_signal_dispatch/uv_run,
not a libc handler.

The original consumer-qualification evidence is retained in the handoff report;
that editor's port/package was subsequently retired. The retained signal stack
restores Neovim's full upstream watcher lifecycle using dormant watchers for
undelivered numbers. Its platform patch drops 829→705 lines, total 3536→3412
(main baseline 735/3515). Native libuv C and two public headers total 4714
lines against 4393 on main. Its upstream patch stays 62 lines. SDK libc also
provides initial-thread pthread_sigmask, preserving errno and returning errors
directly; no port shim or new delivery source is added.

Four one-second whole-QEMU idle Neovim samples were 4/4/3/4% before and
4/6/4/4% after, discarding the initial cumulative sample. The retired timed-read consumer baseline
was 4/4/4/5%; a final-image repeat was 10/8/9/6/6%. An earlier run
was 10/12.9/11/12%. No local build ran during samples, but other host load
and substantial steal time varied. The higher timed-read idle cost is unresolved;
these short nested observations do not establish attribution or native cost.
That comparison used ordinary timed libc reads plus a zero-timeout EOF poll
in place of a native resize/input wait adapter. The observation may matter to
other ports; retirement does not resolve the library-cost question, but it no
longer gates this slice.

You should know: this qualifies the exercised consumers, queued fanout and
close cancellation, not every mask/lifecycle interleaving, output-backpressure
schedule or arbitrary-thread delivery. Nonlocal exits, one-shot libuv watchers,
other signal sources and BusyBox vi's signal bundle remain outside the slice.
Libuv layout changes require consumer recompilation; no kernel ABI or compiler
container rebuild is required. Raw logs, debugger stacks and image hashes stay
in the handoff report.
