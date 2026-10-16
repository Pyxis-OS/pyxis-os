# Native HTTP requests

Delivery is integrated into the [capability model](capability-model-implementation.md).
The native HTTP engine, snapshots, configured endpoints and stdio transactions
are implemented; B8 retires the interim surface and integrates standalone scripts.
No parked prototype is a public interface. Current contracts live in
[HTTP transactions](../interfaces/http-transactions.md),
[fetching](../userland/http-fetch.md), [stdio](../userland/stdio.md#http-resources)
and the [shell](../userland/shell.md#prepared-http-requests).

Consumers remain the [Claude harness](claude-on-pyxis.md),
[Git](git-on-pyxis.md#smart-http-without-libcurl), and
[source fetching](source-builds.md#reaching-the-sources). They use declared roots
and generic transactions rather than private HTTP clients. Git negotiation,
source hash verification and consumer-specific budgets remain consumer work.
HTTP/2, SSE, compression, proxies and general credential storage are not supplied.

**Pyxis must be able to hit good network targets overall in the future.** Track
the end-to-end HTTP/TCP gap against the ThinkPad reference of 2026-10-09:
70.5 MiB/s send and 85 MiB/s receive. These are
[measurements](../development/network-throughput.md), not a contract or nested
QEMU target. OPEN includes receiving the complete snapshot; measure OPEN and
consumption separately. Native B6 A/B′ measured whole GET 12.467 → 13.650 ms
(+9.5%) and POST 36.932 → 29.753 ms (−19.4%); see the
[snapshot record](../development/experiments/capability-http.md#owner-native-ab-prime-2026-10-15)
and [revisit points](../technical-debt.md#http-provider-throughput).

## Accepted owner decisions

HTTP stays in the provider. READ returns a completely framed, re-readable FILE;
WRITE returns a generic staged transaction. A failed outcome is never a
successful prefix. Explicit provider-private RAM or npfs backing bounds storage;
there is no transport-wide body cap or automatic backend fallback. The default
RAM response budget is configurable, and POST keeps its separate body limit.

Configured endpoints capture a held root, selected method, bounded native header
FILE and fresh per-use duration without network I/O. Each operation retains its
captured authority and trust; replacements do not rebind existing holders.
Redirect, credential and error behavior is specified in the
[provider contract](../interfaces/http-transactions.md). Headers/per-root trust
remain [interim credential storage](../technical-debt.md#http-preparation-credentials-and-trust).

## Task 3 accepted facets and lifetime

Generic TRANSACTION independently attenuates STAGE, COMMIT and ABORT. Producers
get a writable native body; control remains with the coordinator. Commit freezes
and drains body writes, submits once, then returns a complete response or typed
failure. Abort stops local work without promising remote rollback. Lost delivered
replies are uncertain and never replayed. Immutable captured error-body snapshots
retain their own bytes independently of the provider's lifetime.

The bounded [native completion interface](../interfaces/native-completion.md) supplies
supported blocking phases. One outstanding completion per process remains a
[limit](../technical-debt.md#native-completion-per-process-admission).
Synchronous DNS and TLS entropy waits remain bounded at 3 s and 5 s.

## Integration of tasks 5b and 5c

Plain C `fopen`/write/flush/read/close and Lua `io.open` use generic libc
transactions; there is no separate HTTP commit function or Lua HTTP adapter.
Explicit flush, final owning close and clean exit follow the
[transaction finalization rules](../userland/stdio.md#typed-resources-and-transactions).
GC/unwind abandonment never submits. Typed captures retain the actual reply;
reading the endpoint again starts a GET, not a replay of the retained response.

Standalone `shell home://post.pxs` uses the declared shell bundle and owns a
subgroup, so stopping the script stops its descendants. Bundle-interpreter
shebangs remain later work. The interim shell table and HTTP-only libc spelling,
options and observation adapters are removed by B8 rather than preserved beside
the capability environment.

## Parked tasks 5b and 5c

The old prototypes (#792 / userland #250 and #799 / userland #254 / ports #110)
were closed in favor of the integration above. Their experiment records qualify
only their recorded revisions, not today's ABI.

## Task 2 accepted TLS policy

Mbed TLS 4.1.1 can need output while reading. Ordinary early responses remain
supported, but a read that attempts TLS output during an outstanding application
write fails explicitly; no accepted plaintext is replayed. A bounded ciphertext
queue is the later alternative if a real server requires it. See
[cooperative TLS](../userland/http-fetch.md#fixed-body-requests).
