# Verified HTTPS snapshots with Mbed TLS

Status: selected next milestone. Mbed TLS is the agreed TLS implementation;
version, configuration and trust provisioning must be settled before runtime
implementation. SSH/libssh is deferred. This follows the closed
[I/O reliability and attribution work](../io-reliability-attribution.md).

## Completion point

An ordinary file consumer opens an `https://` URI through its delegated namespace,
receives a completely fetched immutable FILE snapshot, and consumes it without
knowing about TLS. For example, `cat https://example.com/ | cksum` uses the same
provider bridge as HTTP. A practical final workflow fetches a small, inspected C
source file from a controlled HTTPS endpoint, saves it to `home://` or writable
`host://`, compiles it with TCC, and runs the result.

Success requires authenticated TLS, including certificate-chain, reference-name
and validity-period verification. Invalid trust, unavailable entropy/time,
handshake failures and incomplete responses return useful errors rather than
publishing a partial snapshot or falling back to plaintext.

Existing HTTP, local files and boot without networking remain usable. This is
client HTTPS and read-only snapshots, not a general TLS service or SSH milestone.

## Agreed boundaries

- TLS and certificate processing stay in userspace. The kernel supplies existing
  TCP, randomness, clocks, files and capability enforcement. No TLS syscall or
  kernel HTTP client.
- Use Mbed TLS through its platform/transport interfaces. Keep third-party code
  and configuration in ports, and native transport/authority integration in
  userland. Improve libc only for demonstrated general-purpose gaps; do not add
  successful socket or time stubs to satisfy configuration checks.
- Preserve the existing HTTP/1.1 GET and immutable-snapshot model: bounded whole
  response, existing supported statuses and framing, no redirects, retries of a
  whole request, decompression, cache, connection pooling, credentials or writes.
  Explicit HTTPS does not downgrade to HTTP after failure.
- Namespace publication remains explicit and per session. A scheme binding is
  authority to use the provider, not an allowlist of network destinations.
- No libssh port, TCP listener, remote shell, authentication/account model or
  terminal-session redesign. Future SSH must not dictate today's TLS version.
- No new test infrastructure, boot automation or broad performance project.
  Validate each task with ordinary builds and manual QEMU/debugger use as needed.

## Investigation already performed

A disposable compile probe built Mbed TLS **3.6.7**'s `libmbedtls.a`,
`libmbedx509.a` and `libmbedcrypto.a` with Pyxis GCC 16.2.0 and the existing SDK
(userland `f86daf40ae2c59e00547cc2eb33c19a31731eebb`, parent runtime
`8386424781b0ac8abcb29f43a7f51db2b7f20602`). No upstream C source was changed.

The probe disabled optional NET, TIMING, filesystem I/O, self-tests, PSA
file-backed key storage and default platform entropy; it enabled
`MBEDTLS_PLATFORM_MS_TIME_ALT`. UTC/date support remained enabled. A symbol audit
against SDK libraries and libgcc left `mbedtls_ms_time` as the missing external
platform function. This does not establish a working TLS port: no entropy source
was registered, transport hooks were not implemented, no final application was
linked and no handshake was run. The configuration is not a production profile.

The official `mbedtls-3.6.7.tar.bz2` archive used in that probe has SHA-256
`a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6`.
The [3.6 LTS release notes](https://github.com/Mbed-TLS/mbedtls/releases/tag/mbedtls-3.6.7)
state support through at least March 2027. It is evidence of fit, not an accepted
pin. With SSH deferred, first evaluate the current **4.x line and its bundled
TF-PSA-Crypto dependency**, selecting exact supported revisions during task 1.
The [4.x migration guide](https://github.com/Mbed-TLS/TF-PSA-Crypto/blob/v1.0.0/docs/1.0-migration-guide.md)
explains the crypto split, API changes and CMake build requirement.

A bounded source inspection of libssh 0.12.2 found legacy Mbed TLS crypto APIs
removed in 4.x, plus socket-descriptor and polling dependencies. No libssh build
or port was performed. Shared crypto with a future SSH implementation is a
possibility to revisit, not a compatibility commitment of this milestone.

## Proposed integration and decisions before implementation

The following are recommendations to settle in task 1, not silently accepted
requirements or authorization to implement additional interfaces.

### TLS configuration and native I/O

Propose a client configuration supporting TLS 1.2 and 1.3, with no DTLS, early
data, persistent sessions or client certificates. Use the upstream supported
cryptographic implementation; select algorithms from demonstrated interoperability
needs without implementing cryptographic primitives locally.

Transport callbacks borrow a TCP handle and carry the existing absolute monotonic
fetch deadline. Map positive short transfers, native errors and orderly EOF
explicitly. Account for TLS records exceeding the native per-call payload limit;
a 16 KiB record need not require a larger kernel transfer ABI. Do not restart the
overall deadline for every handshake step or record. Check deadlines between
operations; the deadline does not interrupt arbitrary CPU-bound crypto work.

Entropy comes from the explicitly supplied random capability, with no timestamp
seed or predictable fallback. Supply the required clock hooks using existing
monotonic and UTC facilities and preserve error detection. In particular,
unavailable UTC must not accidentally turn certificate-date verification off.
TLS allocations, certificate-chain limits and input/output buffers need an
explicit bounded-memory configuration alongside the existing body quota.

### Trust and peer identity

Propose a pinned public root bundle packaged in the image and read through an
explicit directory/file grant. Determine its authoritative source, redistribution
notices, installation path and manual update procedure before packaging. A
Mozilla-derived bundle is a candidate; no particular bundle is accepted yet.
The service should retain a stable trust configuration for its lifetime rather
than rereading mutable trust files midway through a connection.

Discuss a deliberate custom-CA configuration for homelab and controlled validation
endpoints. It must be scoped to that provider instance, distinguish replacement
from augmentation of public roots, and never accept roots from the remote peer
as trust anchors. Do not add an insecure verification bypass.

Use the original URI authority for certificate reference-name checks and SNI
where applicable; DNS resolution must not replace that identity with the resolved
address. Settle numeric-IP URI support against the chosen library's IP subject
alternative name verification. If unsupported in the first slice, reject it
explicitly rather than skipping identity verification.

### HTTP framing, publication and errors

Retain the current parser and snapshot limits. Distinguish TLS `close_notify`
from an underlying TCP close. Propose requiring authenticated TLS closure for
close-delimited HTTPS responses; fixed-length and chunked responses must complete
their HTTP framing, without waiting indefinitely for peer shutdown afterward.
Define bounded connection cleanup and error precedence before implementing them.

Reuse libhttp and the existing provider bridge. Settle whether scheme selection
uses an explicit provider mode or another small local arrangement before changing
startup. HTTP and HTTPS publications must have clear lifetimes, grants and
per-instance budgets. Providers must remain discoverable without making network
requests at boot; missing networking should fail a fetch, not prevent local use.

Preserve useful distinctions between trust/identity/date failures, TLS protocol
failures, native transport/deadline errors, HTTP status rejection and resource
limits. Decide how much detail crosses the existing OPEN reply versus provider
logging; add no ABI fields solely to mirror a library's internal error numbers.

## Focused PR tasks

- [ ] **1. Pin/probe and settle the HTTPS contract.** Probe the selected Mbed TLS
  4.x/TF-PSA-Crypto pair with the SDK; record exact sources, checksums, licenses,
  build dependencies and actual compile/link gaps. Settle the decisions above,
  including trust provisioning, numeric addresses, protocol configuration,
  memory bounds and error reporting. Record an update checkpoint. If requirements
  exceed this scope, discuss them before implementing a broader port.
- [ ] **2. Package the TLS libraries and native platform support.** Add the ports
  recipe and development export, narrow demonstrated libc fixes if needed, and
  native entropy/time/transport integration with explicit ownership and failure
  handling. Preserve certificate validation. Validate a manually exercised
  connection against a controlled TLS endpoint; keep any probe executable
  temporary rather than installing another permanent test utility.
- [ ] **3. Add verified HTTPS fetching.** Extend libhttp with URI/port handling,
  handshake, identity verification, encrypted transfers, shutdown/truncation
  handling and structured errors. Preserve HTTP behavior and existing transfer
  deadlines/quotas. Check positive short-transfer handling and failure cleanup.
- [ ] **4. Publish HTTPS snapshots and package trust.** Install the agreed root
  data, wire configuration/grants and trusted service startup, and publish the
  HTTPS provider through existing namespaces. Validate ordinary descriptor/stdio
  consumers, redirection and pipes, separate sessions, service lifetime, and
  no-network boot. Do not give clients extra TCP or trust-management authority.
- [ ] **5. Validate the complete workflow and hand off.** Manually exercise public
  and controlled HTTPS endpoints, the fetch/inspect/compile/run workflow, and
  ordinary HTTP regressions. Confirm rejection of untrusted chains, wrong names,
  invalid validity periods, missing entropy/time and incomplete TLS/HTTP input
  using agreed controlled endpoints/configurations, without kernel fault injection
  or a new harness. Check cleanup with ordinary debugger inspection. Publish
  implemented usage, configuration, update instructions and limitations; move
  this document into docs and update incoming links.

Validation belongs in each implementation PR, not only task 5. Controlled peers
and local CA material should be disposable, with no committed private keys.
Record what was exercised versus inspected, including actual TLS versions and
certificate cases. Public-network availability alone is not reproducible evidence.

## Ownership and later work

Ports owns pinned TLS/crypto sources, recipes, licenses and exported libraries;
userland owns native integration, libc gaps, fetching and service configuration;
Pyxis owns ABI changes only if justified, dependency pins and image assembly.
Follow [repository delivery order](../sdk-and-repositories.md). Do not assume a
compiler-container rebuild: identify any missing host build tools in task 1 and
ask the owner to rebuild only if the existing container actually needs changes.

Current [randomness](../randomness.md) is VirtIO-backed and trusts the hypervisor.
The QEMU milestone can use it. The incoming laptop needs an inventoried and
supported entropy source before native TLS use; a presumed CPU feature is not an
implemented entropy path. Hardware bring-up remains a separate task.

SSH, TLS servers, certificate issuance, automatic trust updates, revocation
policy, mutual TLS, redirects, authenticated HTTP writes, destination confinement
and asynchronous fetching remain separate decisions. Keep relevant limits in
[technical debt](../technical-debt.md) at handoff, without promising them here.
