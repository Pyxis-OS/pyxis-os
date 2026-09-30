# Fastfetch

The normal image includes Fastfetch 2.69.0 at `app://fastfetch.pxe`. The shell
resolves the bare command `fastfetch`. Its upstream MIT license, bundled yyjson
notice, MPL-2.0 license and native port notice are installed under
`app://share/licenses/fastfetch`.

```text
fastfetch
fastfetch --json
fastfetch --config home://fastfetch.jsonc
fastfetch --logo none | cat
fastfetch --json > home://system.json
```

The default text display uses the owner's Pyxis compass-rose ASCII logo and six
information modules. Colors, Break and Separator are also available through
upstream module selection. Explicit JSON/JSONC files use ordinary libc reads
through the caller's namespace; no configuration is discovered automatically.
JSONC accepts comments and trailing commas. CLI presentation overrides and
upstream module formats remain available. The [recipe reference](../../ports/fastfetch/README.md)
contains configuration examples, source pin, patch order and standalone build
instructions.

## Native observations

| Module | Source and meaning |
| --- | --- |
| OS | Pyxis OS name and architecture from `system_info` READ |
| Kernel | Caelum name, architecture and running kernel source commit; ABI page size |
| CPU | Cached guest-visible BSP brand and online logical CPU count |
| Memory | **Memory (allocator)**: allocator total and allocated bytes |
| Uptime | Monotonic duration since HPET initialization through clock READ |
| TerminalSize | Columns/rows from the named output console |

[System information](../interfaces/system-information.md) defines the CPU,
identity and coherent memory replies. These queries do not form one atomic
snapshot. Allocator total excludes permanent reservations and is not installed
RAM; online CPU count is not physical cores or process allowance. Uptime omits
time before HPET initialization and does not establish a calendar boot time.

Unavailable optional fields retain upstream empty/unset formatting and JSON null
values. For example, `{name}{?freq-max} @ {freq-max}{?}` omits unknown frequency;
uptime's `bootTime` is null and calendar placeholders are unset. Native query
failures use upstream module diagnostics. A module error does not guarantee a
nonzero process exit status; allocation and assertion behavior remain upstream.

Actual stdout binding selects automatic plain output. Redirected text may retain
the ASCII logo; `--logo none` suppresses it. The named output grant can still
supply dimensions while stdout is a file or pipe. JSON omits terminal setup and
logo output. Narrow terminals retain upstream layout, so long lines may wrap;
use a shorter format or disable the logo when needed.

The port excludes automatic discovery, config/cache generation, dynamic refresh,
image logos, Lua/JS formats, threads and executable/network helpers. Broader
upstream help remains compiled in; excluded requests follow the documented
unsupported diagnostics or upstream fallback behavior. See
[remaining limits](../technical-debt.md#fastfetch-first-port-boundary).

## Build and integration evidence

`make -j16 image` builds the pinned recipe through the SDK and packages its
executable and notices using the ports install manifest. It needs host CMake
3.21 or newer and the existing compiler; no compiler-container, kernel or libc
change is required for image integration. The upstream formatter, containers,
dispatch, diagnostics and three native-port patches were unchanged by packaging.

Integration was exercised on 2026-09-30 with GCC 16.2.0, CMake 3.31.8, SDK
userland `c9ed311`, and kernel source `9bbb190091d5`. The ordinary image build
passed. Interactive QEMU 10.2.2 with the documented AHCI fix used four CPUs,
CPU `max`, nested KVM, 256 MiB, Fedora OVMF, entropy, virtio-net and a private
virtio-fs export. Fastfetch ran from the boot archive; the export supplied only
configuration, captured output and a disposable launcher.

Local 160x48 and remote 100x30/40x12 invocations completed successfully and
reported their corresponding dimensions. The local logo rendered in full;
narrow output retained long upstream layout lines. Explicit JSONC formatting,
file redirection and a `cat` pipeline worked. Captured plain text and JSON had
zero escape bytes; host parsing confirmed the six JSON results and null fields.
Three separate JSON invocations completed successfully with advancing uptime
and the same allocator total; memory usage changed with other process lifetimes.
This is a functional repeated-run check, not a leak or performance measurement.

GDB compared the running packaged command with its native sources:

- CPU matched the kernel's cached `12th Gen Intel(R) Core(TM) i9-12900K` brand
  and four online CPUs.
- PMM reported 48,747 total and 10,330 allocated frames. The native reply and
  captured JSON both contained 199,667,712 total and 42,311,680 used bytes.
- A clock result of 32,270,385,590 ns became 32,270 ms in the uptime adapter
  and JSON, with no inferred boot timestamp.

A disposable launcher passed only memory and stdout/stderr authority to the
packaged executable. All six data modules produced JSON errors for missing
system_info, clock and named output grants; the process exited zero, matching
upstream behavior. The logo source hash matched the owner's ASCII file. No tests,
fault injection or boot automation were added. These are nested-VM observations;
all validation processes were stopped afterward.
