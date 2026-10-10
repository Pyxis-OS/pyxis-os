# Unpacked bin bundles qualification

Manual nested-KVM qualification on 2026-10-10 for
[bundles in bin](../../../wip/bundles-in-bin.md). Native hardware has not been
qualified. Local fixtures, image variants, JSON captures, screenshots and GDB
logs remain in ignored `build/bin-*`; no test or benchmark infrastructure,
fault-injection code or kernel log lines were added.

## Revisions and configuration

Baseline before code: main `d2ad02a7ce58a3fd516e8dd6630eeee10a34a888`,
userspace `a464b7fe`, ports `e1b3e128`, fs `b427df29`, lwIP `a1aadb91`.
Exact-head CI #1769 supplied all four verified component artifacts. Before-code
initial 1,024-child samples were 3.104 s on one CPU and 3.180 s on four;
startup and session backing were also captured before editing. The table below
uses three subsequent interleaved frozen-control/after pairs per CPU count.

Measured after image: `aee7d8ebfc620ae9985db4a818328c284a97f957`,
userspace `319208f2`, ports `bcc26c27`, unchanged fs/lwIP. Ordinary
`make -j16 image` passed with the existing `pyxis-llvm23.1.3-49e2c1a` container.
A later rebase incorporates the installer restart offer from main #688:
userspace `6b1d8ca3`, same ports, parent `fdd2aa1562642d7379d86354b5528d5727cee29c`.
That ordinary image build also passed. Measured `echo.pxe` and Lua 5.5 `lua.pxe`
are byte-identical in the rebased image. The changed installer is qualified
separately; no compiler-container rebuild or mirror change is required.

QEMU 10.2.2, q35, nested KVM, `-cpu max`, 512 MiB, one socket with one or four
cores/one thread per core, UTC RTC, standard VGA at 1280x800, modern VirtIO
SCSI CD/RNG/network, fresh matching OVMF variables per boot. Both images use
identical staged live configuration: Remote has launcher authority and a Mux
space has launcher/multiplexer authority and writable home. These are local
qualification inputs, not changed shipped defaults. The task’s installer VM was paused, no task build ran, and GDB was detached
during timing. Logging remains the ordinary info level.

After 16 warmup children, time a fresh machine remote session with the existing
client, including command startup, transport, exit and final drain:

```text
lua -e 'for i=1,1024 do assert(pyxis.run{"echo","-n"}==0) end; print("launches=1024")'
exit
```

Use `build/tools/pyxis-remote --machine --no-shell-echo --columns 80 --rows 24
127.0.0.1 24675` with newline-terminated input. All runs completed 1,024 children,
exited zero and reported `drain=complete`. Each pair uses fresh boots, before
then after. Neovim opens a file containing a `test` line with `--startuptime`; the before
entry is its former explicit archive path, after is bare `nvim`. The logfile
reports separate UI-client and embedded-server clocks; do not sum them. These
internal times exclude the shell's lookup/load and command typing.

## Matched costs

| 1,024-child session | Before samples (s) | After samples (s) | Median before / after (s) |
| --- | --- | --- | --- |
| 1 CPU | 3.122, 3.144, 3.113 | 3.132, 3.117, 3.112 | 3.122 / 3.117 |
| 4 CPUs | 3.133, 3.121, 3.171 | 3.177, 3.222, 3.138 | 3.133 / 3.177 |

The four-CPU after median is 1.4% higher; its range overlaps control, and one
pair is lower after. One-CPU medians are effectively unchanged. These short
shared-host intervals show no consistent regression across CPU counts and do
not establish equivalence or a speedup. Inspected direct plain hits still avoid
catalog parsing and directory enumeration; there is no extra candidate lookup
on a successful `.pxe` hit.

| Neovim internal startup | Before samples (ms) | After samples (ms) |
| --- | --- | --- |
| 1 CPU, UI | 27.711, 28.043, 27.066 | 29.667, 27.954, 27.842 |
| 1 CPU, server | 72.132, 69.945, 69.001 | 70.207, 68.598, 68.703 |
| 4 CPUs, UI | 22.477, 27.610, 24.299 | 23.016, 24.312, 22.392 |
| 4 CPUs, server | 44.063, 47.494, 44.803 | 46.510, 48.883, 43.462 |

Ranges overlap for both processes at both CPU counts.

Four-CPU session census repeats the same warmup/launch/editor sequence before
selecting Mux, splitting twice with `Ctrl+B`, `%`, then running `head -c 1 | cat`
in one pane. Type `a` without Enter to complete the waiting pipeline. Read-only
GDB deduplicates the mux/process groups and sums owned `RANGE_BACKED` pages;
borrowed display ranges, page tables and kernel backing are excluded.

| Session | Processes | Owned backing before / after (MiB) | PMM allocated before / after (MiB) |
| --- | ---: | ---: | ---: |
| Mux plus three shells | 4 | 9.08984375 / 9.08984375 | 60.00390625 / 59.75390625 |
| Waiting pipeline | 6 | 11.38671875 / 11.38671875 | 62.39453125 / 62.14453125 |
| Restored prompt | 4 | 9.1640625 / 9.1640625 | 60.078125 / 59.828125 |

The pipeline shell retains 19 heap pages in both warmed runs. No process backing
is leaked by the pipeline. Global heap retains 21 pools before and 20 after
(5.25/5 MiB); live heap bytes are about 10 KiB higher after. This allocator-pool
variation explains the 64-frame PMM difference and is not claimed as a saving.
Allocatable PMM capacity is 70 frames lower after (99,741/99,671); archive/kernel
reserved backing is outside the allocated-frame comparison. Bundled notices
increase archive contents; no causal attribution of the exact capacity change
is claimed.

## Live and installed behavior

A local SDK-linked sample requests a 2 MiB stack and only memory authority.
Bare and explicit bin lookup retain the same selected bundle; read-only GDB
observes 512 backed stack pages, a reserved unmapped guard immediately below,
and the loader's high stack top. The program reads its own `app://` marker,
queries rights 7 (LOOKUP/ENUMERATE/READ_FILES), cannot open it for writing, and
sees no clock grant. It exits normally after one input byte.

Native lookup qualification covers a plain candidate beside a malformed bundle,
a direct bundle hit with an invalid unrelated catalog, a differently named
catalog command and logical bin alias, an incomplete selected bundle returning
CALL_BAD_REQUEST, and a restricted bin candidate returning CALL_DENIED without
fallback. Shell and Lua 5.5 use the common helper; bare Lua 5.1 runs and bare
Neovim reaches the TUI in a tab and a mux pane without a catalog.

A fresh 2 GiB VirtIO target installed and booted without source media on four
CPUs/512 MiB. Bare Neovim and Lua 5.1 launch from npfs; `nvim_runtime://`, bundled
provenance/notices and a deliberately empty bundle directory are reachable.
Offline extraction of the verified ESP archive finds neither bundle tree but
retains a `nvim.pxb-prefix-marker` neighbor. Installed `:help` reads help text;
the same missing-vimdoc-parser warning also occurs on the frozen baseline and live
image; after acknowledging it, help text is readable. This existing editor profile
limit is independent of bundle placement.
Same-revision rerun, two successive real-revision updates, recursive cleanup,
mounted-target refusal and interrupted program-copy/rerun checks remain in progress.
