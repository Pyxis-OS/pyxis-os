# Verified HTTPS snapshots

Ordinary file consumers open `https://` through the delegated namespace and
receive a completely fetched, immutable FILE snapshot. TLS runs in userspace;
the kernel supplies existing TCP, random, clock, file and capability interfaces.
Clients receive snapshot authority without additional network or trust-management
grants. See [fetching and provider startup](http-fetch.md) for commands, URI
handling, HTTP framing, status mapping and snapshot lifetime, and the
[fetch/inspect/compile/run walkthrough](edit-build-run.md#fetch-source-over-https)
for a development workflow.

## Libraries and protocol profile

Ports packages Mbed TLS 4.1.1 with its bundled TF-PSA-Crypto 1.1.1 from the
verified official release archive. The [TLS recipe](../ports/mbedtls/README.md)
records the source, build configuration, licensing and development export.
Userland's `libtls` supplies native authority, allocation and transport hooks;
`libhttp` performs the verified GET and `httpfs --https` publishes snapshots.
The base and guest SDKs remain independent of TLS. Consumers of the exported
upstream headers use the same configuration flags and archives from
`share/mbedtls.mk`; userland tracks the export's content identity so normalized
timestamps cannot leave consumers built against an older structure layout.

The profile supports TLS 1.2 and TLS 1.3 clients with required certificate
verification, upstream software crypto, RSA/ECDSA certificate verification and
the upstream default certificate security profile and authenticated suites/groups.
It disables servers, DTLS, PSK authentication, early data, session tickets,
renegotiation, session serialization and persistent PSA storage. The adapter
does not configure client certificates or restore sessions. It advertises only
`http/1.1` through ALPN; absence of ALPN permits HTTP/1.1, while another negotiated
protocol is rejected. There is no insecure mode or plaintext fallback.

HTTPS currently accepts DNS hostnames only. Verification and SNI use the URI
hostname without its port and with one terminal DNS dot removed; the original
authority remains in the HTTP Host field. Resolution supplies a destination
address, never a replacement identity. Upstream DNS/wildcard matching, including
CN fallback when SAN is absent, remains enabled. Chain, reference-name and
validity-period verification all precede application data.

## Native authority and ownership

[`libtls/tls.h`](../userspace/libtls/tls.h) defines runtime and connection
ownership. One process-local runtime owns parsed trust and PSA/crypto state;
one connection can be active at a time. Allocation and time hooks are
process-local, so this is a single-task interface. No TLS hook discovers startup
resources or borrows an implicit libc clock.

Setup receives explicit random and clock grants. `psa_crypto_init()` eagerly
seeds its generator before trust parsing, under one absolute monotonic deadline
five seconds ahead. All setup entropy reads share that deadline. Native reads
are at most 256 bytes and a partial seed is discarded on any read failure.
Setup makes no network request. There is no timestamp seed, persistent seed or
fallback entropy source.

A fetch borrows its authority and ready runtime, owns its connection and TCP
stream, and frees both before returning a body. Active borrowed authority is
cleared when each operation returns. A retained snapshot owns only its body and
storage reservation. Transport callbacks respect the native 4,096-byte maximum,
report positive short transfers and retry only unaccepted bytes.

Monotonic milliseconds and UTC both come from the supplied clock. UTC is checked
before verification, and failures from time hooks are latched and reject the
fetch even if upstream otherwise reports success. Missing time cannot become a
sentinel that bypasses date checks. Subsequent entropy or reseeding uses the
fetch's authority, with each entropy-hook call capped at the earlier of five
seconds ahead or the original fetch deadline. The shared fetch deadline covers
DNS, connect, handshake and response completion; checks between library operations
do not interrupt arbitrary CPU-bound cryptography.

## Trust configuration and updates

Public roots always load from `app://share/ca-certificates/cacert.pem`. The
[CA recipe](../ports/ca-certificates/README.md) pins curl's Mozilla-derived
2026-09-25 PEM snapshot and preserves its checksum, source/conversion provenance,
MPL-2.0 license and notices. It is 188,900 bytes containing 121 certificates.
The data is installed in the image; loading trust performs no download.

Trusted startup can supply `--ca-bundle URI` to augment public trust for one
`httpfs --https` instance. An augmentation does not replace public roots or
change another instance. Native directory and working-directory grants must be
read-only, as delegated with `service start --read-only` or
`service replace --read-only`. Trust loading uses native files, without provider
lookup. A scheme binding permits use of the provider under existing network
policy; it is not a destination allowlist.

After PSA initialization, setup reads public and configured custom trust,
requires every counted PEM certificate to import successfully, then freezes
the parsed roots. Missing, empty, malformed or over-budget configured trust fails
setup. A failed
import poisons the runtime; no partially accepted bundle is usable. Temporary
input buffers are released before publication. Running providers retain immutable
parsed trust, so changes to backing files require restart or replacement.
Replacement changes future opens; already-open snapshots keep their bytes and
original provider alive until final closure.

The trusted session startup publishes HTTP first, starts HTTPS optionally with
read-only trust authority, then enters the shell. An explicit setup failure is
acknowledged without publishing a grant and leaves existing bindings unchanged.
Optional startup continues after a well-formed failure and successful cleanup.
Missing entropy leaves HTTPS unavailable while local boot and HTTP publication
continue. A missing NIC does not prevent HTTPS publication. Kernel TCP entropy
requirements still apply to both HTTP and HTTPS network fetches.

Updates are manual: review upstream security notices and the TLS/crypto pair,
select a dated root snapshot, review certificate changes, verify its published
checksum, update ports pins/provenance/notices, rebuild the image and restart
providers. Ordinary builds use pinned inputs, with no moving trust URL or
automatic update. Publishing changed dependencies follows
[repository delivery order](sdk-and-repositories.md).

## Memory and failure semantics

Each HTTPS provider has a hard 2 MiB counted allocation budget for trust-file
input, parsed trust, TLS/crypto state, transient allocations and bookkeeping.
It is independent of the HTTP body-storage budget and is not a process RSS
limit. Separate provider instances have separate budgets. TLS input/output
record content buffers remain fixed at 16 KiB each; handshake messages must fit
the bounded input buffer. Verified chains retain the upstream maximum of eight
intermediate CAs. These bounds apply before accepting peer-controlled data.

The selected roots and controlled TLS 1.2/1.3 connections fit the cap: observed
charged trust baselines were 355,443 bytes for public roots and 357,602 bytes with
the disposable custom CA; peak charged demand including trust-file input was
547,786 bytes. These are requested allocations plus accounting headers for the
measured configuration, not allocator backing pools, process RSS or a guarantee
for arbitrary chains and suites. Cleanup returns to the trust baseline; runtime
retirement releases trust and crypto state.

Certificate verification and PEM-import rejection map to `CALL_DENIED`; native
trust-file failures retain their setup status. TLS protocol failure and
truncation map to `CALL_IO`. Native clock, entropy, transport and deadline errors
retain their call status. Allocation-cap exhaustion maps to `CALL_QUOTA`,
allocator failure to `CALL_NO_MEMORY`, and recognizable encoded-size limits to
`CALL_FILE_TOO_LARGE`. Library results retain the TLS category, upstream code
and certificate flags; OPEN retains the final HTTP status separately, or zero
when absent. No ABI fields were added for upstream codes.

Only verified, completely framed responses become snapshots. Failure releases
staged body/TLS state and aborts the stream, preserving the first failure over
cleanup diagnostics. After completion, local TLS shutdown gets at most 100 ms
of the original remaining deadline and does not wait for peer shutdown.
Shutdown failure is diagnostic after valid framing; native handle-close failure
still fails the fetch. See [HTTP framing and cleanup](http-fetch.md#request-and-response-policy)
for authenticated EOF and incomplete-response rules.

## Validation scope

Manual closure checks used nested KVM QEMU with two vCPUs, 256 MiB, VirtIO
networking/randomness and the pinned runtime/provider implementation. Public
HTTPS with packaged roots alone fetched `https://example.com/`: the guest and
host matched at 713 bytes and CRC 1346324142. Controlled peers supplied TLS 1.2
and TLS 1.3 responses and disposable certificates; no private keys or probes
are installed in the normal image.

| Check | Observed result |
| --- | --- |
| Trusted, correctly named expired and not-yet-valid leaves | Certificate rejection, flags 1 and 512, before GET |
| Wrong name and untrusted chain | Certificate rejection, flags 4 and 8, before GET |
| Raw EOF in close-delimited TLS | TLS truncation; no snapshot |
| Authenticated EOF before fixed/chunked HTTP completion | Malformed HTTP; no snapshot |
| Missing clock authority in a disposable native probe | Setup rejected with `TLS_CLOCK_ERROR`/`CALL_BAD_HANDLE`; fetch rejected with `CALL_DENIED` before network activity |
| Fetch, save, inspect, TCC compile and execute | An 88-byte controlled C source matched CRC 3122158114 and printed its expected message |
| HTTP and valid TLS 1.3 chunked responses | Matching 12,000-byte bodies, CRC 4154493573 |

GDB inspection after the exercised fetch failures found no body allocation or
active TLS connection and exact recovery to the parsed-trust baseline. Framing
failures retained HTTP status 200 as diagnostic metadata. The missing-clock
probe left no runtime owner or borrowed authority after cleanup. This checks
absent clock authority; unavailable UTC from an otherwise valid clock grant
was inspected in code, not induced by a kernel modification.

Earlier integration checks established no-NIC publication, no-RNG setup failure
with continued local/HTTP startup, independent session trust, and retained
snapshots across provider replacement. These are functional observations for
the selected peers and configurations, not exhaustive cipher/chain coverage or
performance measurements. Builds and temporary-validation cleanup accompany
the changes; future source or trust updates require renewed validation.

## Accepted limits

This is read-only client HTTPS. TLS servers, SSH, certificate issuance, mutual
TLS, automatic trust updates, redirects, authenticated writes and asynchronous
fetching require separate scope. Platform trust, revocation, numeric HTTPS
identity and deadline limits are recorded in
[technical debt](technical-debt.md#https-trust-and-platform-limits).
The existing [startup reporting limitation](technical-debt.md#service-startup-failure-before-publication)
still applies when a provider exits before reporting setup outcome.
