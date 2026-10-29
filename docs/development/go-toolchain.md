# Native Go cross-toolchain

The [pyxis-go fork](https://git.internal/PyxisOS/pyxis-go) adds a static
`pyxis/amd64` target to Go 1.27.3. Its linker writes P1F directly; no converter,
libc or Linux syscall layer is involved. This first profile supports native
runtime bootstrap, parallel Ps, native files/streams and IPv4 TCP/UDP/DNS,
fmt/time and HTTP/TLS clients, with normal multi-M/GC and cooperative Go preemption. See the [runtime milestone](../wip/go-runtime.md)
and the [startup](experiments/go-native-hello/README.md) and
[parallel](experiments/go-parallel/README.md) and
[files/time](experiments/go-files-time/README.md) and
[readiness/capacity](experiments/go-readiness-set/README.md) records for limits.

## Build and run

Use a Linux amd64 host, git and a Go bootstrap supported by Go 1.27.3. This is
separate from the LLVM compiler/container and ordinary image builds. Source is
fetched shallowly from the internal fork at the exact commit in
[`build-go.sh`](../../toolchain/build-go.sh); no upstream or module fallback:

```sh
GOMAXPROCS=8 GOROOT_BOOTSTRAP=/usr/lib/golang \
  toolchain/build-go.sh "$HOME/opt/pyxis-go" "$PWD/build/go-work"
toolchain/build-go-hello.sh "$HOME/opt/pyxis-go" "$PWD/build/gohello.pxb"
make -j16 image DISTRIBUTION=full GO_HELLO="$PWD/build/gohello.pxb"
```

The prefix and source directory must be fresh. The installed GOROOT contains
its sources and Linux tools, full Go license and exact fork revision. The hello
bundle retains that license and provenance. `GO_HELLO` stages only the selected
bundle in the full live archive; empty is the default. The default core archive
does not accept this probe input. Rescue remains unchanged.
Run `boot://gohello.pxb` from a live shell. Native memory/thread/clock/entropy authority must be
available to the bundle's declared memory, threads, clock and random grants.
The clock requires `read`, `precise` and `sleep`: the runtime checks all three at
startup, then uses precise monotonic/UTC reads for timers and `time.Now`. Missing
authority fails startup; there is no coarse or ambient replacement. No new compiler container is needed.

For another pure Go program that fits this profile:

```sh
GOOS=pyxis GOARCH=amd64 GOAMD64=v1 CGO_ENABLED=0 \
  "$HOME/opt/pyxis-go/bin/go" build -trimpath -o hello.pxe hello.go
```

Package it in a bundle with the same explicit grants as
[the hello manifest](../../toolchain/go-hello/manifest.json). The runtime owns
native startup/TLS; never link libc startup into it. Exported text entries become
Go environment strings, while capability entries stay native held values.

## Initial limits

The target defaults to one P and `runtime.NumCPU()` reports one; there is no
automatic native CPU-count discovery. An exported `GOMAXPROCS=4` or `8`, or
`runtime.GOMAXPROCS(n)`, enables parallel Ps independently. These settings and
dynamic changes are qualified on four/eight-vCPU nested guests. Native thread
admission still includes sysmon and blocked Ms in addition to active Ps. Required
thread/memory exhaustion reports the native operation/status and terminates the
process; the runtime does not promise a P count beyond native admission.
Native Ms use guarded stacks and FS blocks, and
only native completion permits their release; idle retired contexts are collected
before another M is created or at process exit. Reservation bookkeeping pages
remain until process exit and reuse vacant slots.

Go safe points provide cooperative preemption. A call-free loop can indefinitely
delay another goroutine or GC. Hardware faults terminate the process; explicit
panic/recover works. Only amd64/v1 without cgo, external linking, PIE or dynamic
dependencies is supported. Qualified kernels preserve x87/SSE/AVX;
CPUID.OSXSAVE and XGETBV gate Go's
optional AVX/AVX2 and SHA paths. A fallback boot retains the legacy feature
limits. This does not change the supported Go build baseline or enable FSGSBASE.
See [FP ownership](../kernel/userspace.md#floating-point).

Runtime diagnostics use blocking writes to the supplied console, pipe or FILE,
including under debuglock. Native calls carry up to 4096 bytes (4080 data bytes
for inline FILE writes); backpressure retains that P and can stall a one-P
program. Networking, hosted compilation, profiling and a complete standard-library
build remain later work. Runtime bootstrap entropy retries QUEUE_FULL within one
five-second deadline;
other failure is fatal, with no predictable fallback. Before g/m, bootstrap
failures report a typed terminal startup result; the launching parent names its
step/status even without usable stdio. This needs no FS, heap, lock or output
grant. Once g/m exist, fatal native
hooks report the operation/status and throw a traceback. Missing wall time fails
explicitly rather than inventing a clock. Scavenging DECOMMIT metadata exhaustion
retains and clears the affected pages; it preserves zeroed reuse but can retain
RAM until later use or whole-identity release.

## Native files, streams and time

`os.File` owns an actual native handle, exposed as a borrow by `Fd`; `NewFile`
adopts one on success. Resolve `name://` against held environment/package roots,
or relative paths against the retained cwd directory chain. There is no ambient
slash root. Standard streams preserve their supplied interfaces and grants.
FILE-backed output shares confirmed positions with runtime diagnostics; independent
opens remain independent. Supplied stdout/stderr append companions select
native APPEND for ordinary output and diagnostics, with atomic EOF selection
per operation of at most 4,080 bytes. Larger writes may interleave; no sync or
append-only authority is implied. Unsupported backings (including HOST) refuse
before create/truncate, using a zero probe or required CREATE admission.
Malformed companions and unsupported streams fail I/O without stopping package
initialization; duplicate adoption fails. Native metadata validity is visible in `FileInfo.Sys()`
(`syscall.Stat_t.Valid`), without invented Unix permissions or timestamps.

Open/create, read/write, positioned I/O, seek, truncate, sync, enumeration and
native directory mutations and `O_APPEND` work where granted. `os.Root`, exported
providers and process execution fail explicitly in this slice. A native FILE
value received through a supplied pipe is read to EOF before the next segment;
exported FILE values fail before provider I/O and stay owned until close.

Ordinary pipe/console waits park goroutines through one lazy native
[readiness set](../interfaces/readiness-sets.md). Creation needs no grant; add
`pipe/create` only for programs needing `os.Pipe`. Imports, immediately successful
I/O and timer-only programs create no set or wake pipe. Dynamic descriptor
metadata replaces the 31-registration table; kernel metadata across all sets of
a process is bounded by 16 MiB, with explicit resource refusal. Ready replies
contain at most 64 records, independently of the number registered. Close wakes
Go waiters and joins native unregister before closing/reusing the handle.
A read deadline on console stdin wakes a parked `Read`. Terminal programs use
`syscall.TerminalMakeRaw`, `TerminalRestore`, `TerminalSize` and
`TerminalIsTerminal`. `(*os.File).WaitTerminalResized(generation)` parks until
the terminal's geometry generation differs from the one `TerminalSize` returned;
resizes coalesce, so the caller queries the size again and waits with the new
generation. It does not hold the read lock, so it runs beside a blocked `Read`,
and only Close cancels it.
An opened directory keeps the rights of the handle it came from, so `Sync` on
it works where that directory grants CREATE or REMOVE.
Recoverable console input-loss or keyboard-unavailability events wake a native
read recheck without permanently poisoning the Go descriptor. Acknowledging loss
and restoring input works on the same stdin handle. INPUT_LOST and UNAVAILABLE
reach the program as read errors: for example, `bufio.Scanner` stops at its first
overflow error; a subsequent read can recover through that same stdin.
Many idle streams do not each require a native M. Regular FILE calls release
the P during native I/O; they have no readiness or deadline facility.

Go's initial native-M limit is 256, matching the kernel's total live/retiring
task capacity. Its table holds 256 noninitial contexts; the initial M is separate,
so the native total permits at most 255 siblings. A definitive CREATE LIMIT
without an observer can wait for a known retiring peer's actual completion and
retry while holding the creation lock. Other creators spin with `osyield` until
that bounded retirement/collection finishes; this supplies no wall-time guarantee. No live context, uncertain CREATE or backing is reused; genuine capacity
or allocation exhaustion still fails explicitly. The libc wrapper's independent
64-context bound is unchanged. The poller retains its geometric index and
reusable descriptor metadata high-water backing until process exit.

Timers use monotonic time. `syscall.ClockWallNow()` reports native wall-time
availability; `time.Now()` fails explicitly when unavailable. `time.Local` reads
the session-configured `TZ` and loads the archive's `boot://share/zoneinfo/<name>`
through the supplied boot grant. A bad/missing named zone fails explicitly.
Absent/empty `TZ` retains the existing UTC default. `time.LoadLocation` uses the
same archive; there is no embedded copy or host timezone-path fallback.

The readiness and thread-sizing prerequisites and native network adapters are
qualified on four-vCPU nested QEMU. Native hardware qualification remains open;
The optional [Caddy package](../userland/caddy.md) is QEMU-qualified for the
recorded HTTP/supplied-TLS profile.

## Standard-library platform adapters

`crypto/rand` fills through the held native RANDOM grant. Each bounded native
chunk and its QUEUE_FULL retries share a five-second deadline; the whole fill
has no size-dependent deadline. It has no
predictable fallback. `path/filepath` treats `name://` as a lexical native root
and uses semicolon-separated lists; spelling a path grants no authority.
`os/user` lookup/group operations return explicit unsupported errors.

`os/signal` lazily creates one process-local receiver for `os.Interrupt`.
Readiness parks its delivery goroutine; imports create no receiver or waiter.
Notify sends without blocking on the channel. Stop/Reset join detachment;
Ignore consumes events and stays active until Reset, including across Notify.
Other signal values are accepted but never delivered and report not ignored.
The shell's second Ctrl+C and native TERMINATE/group stop remain forceful.
Resource/transport failure of the void subscription API is an explicit fatal
runtime operation/status; normal registration is supported, and the upstream
nil-channel argument panic remains. Owned script/capture scopes still force
stop on the first press, as recorded in [debt](https://git.internal/PyxisOS/pyxis-os/issues/1360).

`runtime/debug.SetCrashOutput` owns a native copy independent of caller close.
Replacement/disable joins actual runtime writes before release. FILE output
shares confirmed positions and supplied append semantics; unsupported types or
rights refuse before changing the current target. Crash output uses the same
native APPEND policy when selected; fatal output is still blocking. Empty ordinary
writes are no-ops and keep the cursor, while `Pwrite` remains positioned.

Public roots are implemented as a lazy read of the held
`boot://share/ca-certificates/cacert.pem`, with Go verification and no host/env
root scan. Its strict parsing rejects the entire configured bundle on any bad
certificate; a bad update to the [CA pin](../../ports/ca-certificates/README.md)
can therefore disable default TLS trust. Applications can still supply explicit
CertPools or register upstream SetFallbackRoots (including through an
x509roots/fallback import). A registered pool is used after a failed system-root
load; GODEBUG=x509usefallbackroots=1 forces it even when system roots are valid.
Thus fail-closed describes the default loader, not an application-chosen trust
policy. Pyxis embeds no fallback pool. Target X.509 qualification waits on Go
networking. `os/exec` builds, but
`LookPath` returns an error matching both `exec.ErrNotFound` and
`errors.ErrUnsupported` for every name or
path, and subprocess launch remains unsupported. No PATH search is performed.
Age and subprocess execution remain unqualified. See the historical
[adapter record](experiments/go-library-adapters/README.md) and the separate
[Caddy delivery](experiments/caddy/README.md). Go's MIME package uses its builtin
extension table, without an ambient platform database; applications can register
more types with `mime.AddExtensionType`.

## Networking

Native IPv4 TCP uses TRY plus the shared readiness-set netpoller for streams and
listeners. CONNECT and UDP SEND/RECEIVE use native completions, one owned receiver
per pending operation. Collection parks a Go goroutine; cancellation joins the
actual outcome, returned grants and receipt acknowledgement before reuse. The
existing syscall file registry and `internal/poll.FD` own handles. UDP deadline
state lives in that FD; there is no second close/ownership map or native M per
idle socket.

Alongside the ordinary Go runtime grants, a dialing bundle requests:

```json
{"name":"tcp","resource":"tcp","rights":["connect"],"required":true},
{"name":"service","resource":"service","rights":["create"],"required":true}
```

Datagrams also need `udp/open`; default hostname resolution needs the supplied
`dns_udp/open` and `DNS_SERVER` pair. The service constructor is an explicitly
requested private runtime role and is not implicitly forwarded. It creates
endpoints/exports generally, not only completion receivers; see the
[grant-all consequence](https://git.internal/PyxisOS/pyxis-os/issues/1499).
Imports alone create no receiver. A missing required grant refuses launch;
a deliberately omitted constructor produces an operation error, not an init panic.

Default DNS captures configured server text and its factory together on first
use. It uses Go's DNS parser and that held UDP scope only: no hosts/resolv.conf,
search suffix, public fallback or wider general TCP factory. A truncated default
UDP reply reports unsupported TCP fallback. An explicit `Resolver.Dial` supplies
its own transport; if configured server text is missing/invalid it receives an
empty address and must select its own endpoint. Numeric addresses bypass DNS.

A server needs separately supplied TCP LISTEN authority, a concrete configured
IPv4 address and nonzero port. Normal interactive CONNECT grants do not authorize
it; the qualification uses an existing trusted init handoff. Wildcard/ephemeral
TCP listeners, IPv6, raw/Unix sockets, multicast/OOB, FileConn/FileListener and
socket options without a native operation return unsupported errors.

TCP transfers retain native partial I/O, EOF and write half-close. Native Close
aborts unless write shutdown was committed and incoming data drained; callers
requiring orderly teardown use `CloseWrite` and drain EOF. Close is not delivery
acknowledgement. UDP receives consume complete native datagrams and truncate only
the returned copy; empty datagrams return zero without EOF. Maximum datagram size
is 1,472 bytes. Native CONNECT/RECEIVE windows are at most 30 seconds, shortened
by earlier public deadlines. SEND keeps one native five-second ceiling and a
rearmable public expiry timer. Extending/clearing its public deadline leaves the
same SEND pending; an earlier deadline rearms expiry and a past one cancels now.
Expiry/Close join actual cancellation and collection; confirmed acceptance keeps
its actual count. Infinite receives repeat only confirmed empty timeouts or joined
deadline-change cancellation. CONNECT/SEND never replay after uncertainty.

The [networking record](experiments/go-networking/README.md) separates QEMU
functional/authority checks, native task observations and nested timings. It does
not establish native hardware behavior, Caddy/HTTP3 support or service throughput.
