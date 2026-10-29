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
