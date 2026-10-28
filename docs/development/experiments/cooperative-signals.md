# Cooperative libc signals: initial qualification

2026-10-28, nested Q35/KVM with host CPU, four vCPUs, 4 GiB, VirtIO GPU/net,
HDA with null audio and a writable virtio-fs export. Full RAM-live archive;
`init=boot://boot-init.pxe nvme.write=0 display.timing=off log.udp=1`.
Existing LLVM 23.1.3 compiler image `8bdf136`; no compiler rebuild, new tests,
probe programs or boot/output automation.

## Revisions and build

Before implementation, the full image at parent `79ccea04`, userland `2101e916`
and ports `18b56bf0` built and booted. Final code qualification here used parent
`67921e28`, userland `d78cb0ff` and ports `32efdeec`; later restacks and their
matching checks are recorded in the PR. Initial composition failures identified
newly exposed Links job-control/child branches, its duplicate raise fallback,
and Fastfetch descriptor helpers hidden by a Pyxis header guard. Original port
patches/config select actual supported behavior; no fake kill/wait was added.
The corrected full image build passed.

## Observed behavior

- Fastfetch's unchanged upstream interactive generator rendered, accepted a
  selection/save and produced a readable JSON config. Idle mux split/zoom
  changed real terminal geometry and the UI redrew. Its native helper now
  shares the existing complete-write loop: a single upstream frame write had
  previously lost the tail after the native console accepted its 256-byte
  prefix. Positive partial transfers remain valid libc behavior.
- BusyBox less rendered a held file, redrew on idle mux resize, and reached
  line 120/END with `cat FILE | less`, then quit normally. Its upstream poll
  loop received cooperative WINCH; the mask-aware helper also builds unchanged. Forced mux-pane
  close also restored a usable remaining shell.
- Links rendered the local held HTML page and quit.
- Official Lua blocked in `io.read("*a")`; one Ctrl+C force-stopped it, including
  when launched by its explicit image path. The shell matches interpreter image
  identity and force-stops interpreter/helper scopes. This confirms that policy
  remains in effect; it does not qualify libc INT delivery.

Four one-second idle-less host CPU samples, after discarding the cumulative
first sample, were 4/4/3/4% before and 3/4/3/3% after (one host CPU). No local
build ran during sampling. These short nested-VM observations overlap and show
no idle hot loop; they are not a latency bound or a native performance claim.
Main's unrelated Intel inventory changes also landed between the images.

## Failures and remaining qualification

Resizing BusyBox less during an unfinished search prompt failed: the prompt
filled with backslashes and stopped consuming input. Source inspection shows
upstream getch_nowait returning backslash while winch_counter is nonzero, while
less_gets never returns to the outer loop that clears it. No platform rewrite
was added; the short upstream draft and screenshots are in the handoff packet.

Fastfetch requests autowrap-disable (`CSI ? 7`), outside the current terminal
repertoire. Long help rows wrap in narrow mux panes; full-width rendering and
module reflow work. This is a terminal compatibility follow-up, not signal loss.

INT receiver/callback behavior, local raise/pending, sigsuspend, pselect and
RESETHAND/NODEFER/SIGINFO combinations have source review but no direct runtime
qualification from these consumers. The accepted no-new-test-program rule
remains in effect; a suitable real non-interpreter consumer is still needed.
Worker and nonlocal handler exits retain the documented unsupported profile.
See the [contract and rebuild needs](../../userland/cooperative-signals.md).

You should know: the draft stack is reviewable, but the milestone checkbox stays
open. An upstream UI defect and source-only API coverage are not reported as
passed runtime checks.
