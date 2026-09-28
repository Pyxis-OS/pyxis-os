# Verified HTTPS snapshots with Mbed TLS

Status: task 1 complete. The source pair, trust model and contract below were
agreed on 2026-09-28. They describe the implementation target, not a working TLS
port. Task 2 is next; no runtime integration or dependency-pin changes are part
of task 1. SSH/libssh remains deferred. This follows the closed
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
pin. Task 1 selected the **4.x line and its bundled TF-PSA-Crypto dependency**
below; the 3.6 probe is historical evidence only.
The [4.x migration guide](https://github.com/Mbed-TLS/TF-PSA-Crypto/blob/v1.0.0/docs/1.0-migration-guide.md)
explains the crypto split, API changes and CMake build requirement.

A bounded source inspection of libssh 0.12.2 found legacy Mbed TLS crypto APIs
removed in 4.x, plus socket-descriptor and polling dependencies. No libssh build
or port was performed. Shared crypto with a future SSH implementation is a
possibility to revisit, not a compatibility commitment of this milestone.

### Task 1: current 4.x probe, 2026-09-28

Selected: Mbed TLS **4.1.1** and its bundled TF-PSA-Crypto **1.1.1**.
The [Mbed TLS release](https://github.com/Mbed-TLS/mbedtls/releases/tag/mbedtls-4.1.1)
and [crypto release](https://github.com/Mbed-TLS/TF-PSA-Crypto/releases/tag/tf-psa-crypto-1.1.1)
are from the supported LTS branches, supported through at least March 2029.
At this checkpoint, 4.2.0 is the current feature release; we selected the LTS
branches for their maintenance window.

Exact source identities:

| Input | Revision / SHA-256 |
| --- | --- |
| Mbed TLS tag `mbedtls-4.1.1` | `0a8fda272a5a0abef3b47c91bed37185d5a726b1` |
| Bundled crypto / tag `tf-psa-crypto-1.1.1` | `a0632be94d883daa0295ac1eabf70359ad94f91b` |
| [Official Mbed TLS archive](https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-4.1.1/mbedtls-4.1.1.tar.bz2) | `3359a349e23db3d5536fcee032ae7b2ecbfc08972fab643089b5cbf2a375c98c` |
| [Official standalone crypto archive](https://github.com/Mbed-TLS/TF-PSA-Crypto/releases/download/tf-psa-crypto-1.1.1/tf-psa-crypto-1.1.1.tar.bz2) | `3236f70e827fa693ae9e97499957a80c6dba25395d4617b7d21ecb60c82a480e` |

Both downloaded archive hashes match upstream. The Mbed TLS release-note prose
says it upgraded to crypto 1.2.0, but its Git tree pins the 1.1.1 commit above,
the bundled CMake version is 1.1.1, and a recursive content comparison against
the standalone 1.1.1 archive found no differences. Use the verified source
identities, not that conflicting release-note sentence. Both projects offer
Apache-2.0 OR GPL-2.0-or-later; select Apache-2.0 and preserve their complete
LICENSE files and source notices when packaging.

`make -j16 sdk` succeeded at parent `21274c2`, userland
`f86daf40ae2c59e00547cc2eb33c19a31731eebb`, with Pyxis GCC 16.2.0.
Unmodified upstream C sources compiled into `libmbedtls.a`, `libmbedx509.a`
and `libtfpsacrypto.a` using CMake 3.31.8 and `make -j16`. The cross file used
`CMAKE_SYSTEM_NAME=Generic`, `CMAKE_SYSTEM_PROCESSOR=x86_64`,
`CMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY`, the SDK sysroot and its explicit
freestanding/baseline x86-64 header and compiler flags. Inspection of generated
compile commands found that upstream CMake appends `-std=c99` after the SDK's
`-std=gnu23`: the upstream probe actually compiled as C99. SDK/native code remains
GNU C23. No host C library or host headers were used for target code.

CMake options were `ENABLE_PROGRAMS=OFF`, `ENABLE_TESTING=OFF`, `GEN_FILES=OFF`.
Two external user-configuration headers made only these changes to defaults:

```c
/* MBEDTLS_USER_CONFIG_FILE */
#undef MBEDTLS_NET_C
#undef MBEDTLS_TIMING_C

/* TF_PSA_CRYPTO_USER_CONFIG_FILE */
#undef MBEDTLS_FS_IO
#undef MBEDTLS_SELF_TEST
#undef MBEDTLS_PSA_CRYPTO_STORAGE_C
#undef MBEDTLS_PSA_ITS_FILE_C
#undef MBEDTLS_PSA_BUILTIN_GET_ENTROPY
#define MBEDTLS_PSA_DRIVER_GET_ENTROPY
#define MBEDTLS_PLATFORM_MS_TIME_ALT
```

A target link probe with all three archives under `--whole-archive`, SDK startup,
libc/libpyxis and libgcc failed only on `mbedtls_ms_time` and
`mbedtls_platform_get_entropy`. No successful dummy implementations were added.
This identifies platform hooks, not a completed port: UTC currently resolves
through libc's startup clock lookup, native transport and explicit-authority
clock hooks are still required, and this broad default profile is not the final
client configuration. No target executable, handshake, memory measurement or
QEMU validation resulted from this probe. No general libc gap was demonstrated.

The official archive includes dependencies and generated sources. Its library
build needs CMake (the release README documents 3.20.2 or newer), Make and the target C
compiler; Python is optional for configuration warnings with generation disabled.
Archive fetching/extraction also needs curl, SHA-256 tooling, tar and bzip2.
The checked-in builder recipe does not install CMake; no published container was
available locally to inspect. Task 2 must arrange that host tool before its CI
build. GCC/binutils themselves need no rebuild. The existing ports runner only
fetches a Git commit and does not initialize submodules; an archive recipe needs
a narrow packaging arrangement, not an assumption that today's runner already
supports release archives. Do not replace the official archive with an incomplete
GitHub-generated source snapshot.

The temporary sources, user-config headers, cross file and logs are in
`/tmp/pyxis-https-task1`. This location is disposable; source identities and probe
settings above are the durable record. No QEMU/debugger jobs were started.

## Agreed implementation contract

### Protocol and identity

Use the selected 4.1.1/1.1.1 pair for TLS 1.2 and TLS 1.3 clients with required
certificate verification. Disable server/DTLS support, early data, session
resumption/tickets, renegotiation, PSK authentication and client certificates.
Use upstream software crypto and its default certificate security profile;
retain RSA/ECDSA certificate verification and the upstream certificate-authenticated
TLS suites/groups initially. Do not weaken the profile to accommodate an endpoint.
Advertise only `http/1.1` if using ALPN; absence of ALPN permits HTTP/1.1, but a
negotiated different protocol is unsupported. The final client build and its
actual negotiated suites belong in task 2's validation record. The broad compile
probe above does not establish interoperability for this narrower profile.

HTTPS accepts DNS hostnames only in this milestone, with port 443 by default
and the existing explicit-port syntax. Numeric HTTPS URIs return the existing
unsupported-operation result (`CALL_BAD_OPERATION`). This is a scope choice:
source inspection found IP SAN matching, but the general name verifier also
accepts DNS/CN matches and the hostname setter feeds SNI. Numeric IPv4 needs
deliberate IP-only identity verification and SNI handling in a later task.
Ordinary HTTP numeric-address support stays as is; IPv6 remains unsupported.

Use the URI hostname, without its port and with one terminal DNS dot removed,
for the TLS reference name and SNI. DNS resolution supplies only the destination
address. Keep the original URI authority for the HTTP Host field. Use upstream
DNS/wildcard verification, including its CN fallback when SAN is absent; never
skip identity checks. There is no plaintext retry after HTTPS failure.

### Native authority, ownership and deadlines

Fetch borrows explicit TCP, UDP, random and clock grants, stable parsed trust and
the provider's TLS configuration. It owns its connected TCP handle, per-fetch
TLS state and body staging. Transport callbacks borrow that stream; an immutable
snapshot retains only its body/storage ownership, never TLS or TCP state.
Native hooks must not perform startup-resource discovery on behalf of libhttp.

Keep one absolute monotonic deadline across DNS, connect, entropy, handshake,
HTTP and successful response validation: the earlier of the caller's deadline
and the existing 30-second fetch budget. TCP callbacks split transfers at the
native 4,096-byte limit, return positive short transfers accurately and retry
only unaccepted bytes. Do not extend the deadline on retries or TLS record
boundaries. A 16 KiB TLS record needs no larger kernel transfer ABI. Check the
deadline between library operations and before accepting the response; it does
not interrupt arbitrary CPU-bound crypto work.

Use the upstream PSA random generator seeded by
`mbedtls_platform_get_entropy` through the supplied random capability. Native
reads are at most 256 bytes, with deadlines capped at the earlier of the fetch
deadline and five seconds ahead. Treat a partially gathered seed as failure if
any native read fails. No timestamp seed, persistent seed or alternate source.
The QEMU contract trusts the VirtIO host's bytes, as described in
[randomness](../randomness.md); it does not claim independent guest entropy.

Supply monotonic milliseconds and UTC hooks using the supplied clock. Precheck
UTC and latch errors from clock hooks during certificate verification, rejecting
the fetch even if the TLS library otherwise reports success. Do not rely solely
on libc `time()` or allow a failure sentinel to bypass validity checking.
The hooks and PSA state are process-local: one HTTPS provider handles one fetch
at a time, and its active borrowed authority is valid only during that fetch.
Set up shared state without reading entropy or making network requests at boot;
initialize/reseed the generator under a fetch's authority and deadline as needed.

### Trust provisioning

Package public roots by default. An optional custom-CA bundle **augments** those
roots for that provider instance; it does not replace public trust or alter
another instance. Trusted startup supplies read-only file/directory authority
and the optional bundle path. Clients opening HTTPS files receive neither trust
configuration authority nor extra network grants. Never accept roots supplied
by a remote peer as trust anchors or provide an insecure verification bypass.

Select curl's Mozilla-derived
[2026-09-25 PEM snapshot](https://curl.se/ca/cacert-2026-09-25.pem), 188,900 bytes,
121 certificates, SHA-256
`a41b5d356aea97a529fe27e0f7316d2f9d946d75927476cf9cf1b90637d00505`
(downloaded and matched to its published checksum). Install at
`app://share/ca-certificates/cacert.pem` with MPL-2.0, source/conversion provenance
and retained notices. The [CA extract documentation](https://curl.se/docs/caextract.html)
warns that this PEM export omits Mozilla's additional trust-store constraints;
it is not the full browser trust policy.

Read and parse the public and any configured custom bundle before publishing
the provider, then release temporary input buffers and keep immutable parsed
trust for its lifetime. Missing, malformed, empty or over-budget configured
trust fails that provider's setup; do not silently ignore custom-bundle errors.
Local files and HTTP remain usable. Changes to backing files do not change a
running provider; restart it to change trust. No network request is needed to
load packaged trust, and a missing network/entropy device fails a fetch rather
than local boot.

Manual updates select a dated snapshot, review certificate changes, verify its
checksum, update ports metadata/notices, rebuild the image and restart providers.
No boot-time download or automatic trust update. Custom roots have the same
certificate-verification rules as packaged roots; augmentation is deliberate
additional trust, not destination confinement.

### Memory limits

Keep existing HTTP limits: 16 MiB per body, 64 MiB per provider's body-storage
reservations, 32 KiB aggregate headers/trailers, 256 fields, eight informational
responses and separately bounded parser/request scratch.

Add a hard **2 MiB allocation budget per HTTPS provider** for trust loading and
parsed trust, TLS/crypto state, and transient TLS allocations, including overlap
during replacement. Charge temporary trust input and allocation bookkeeping to
that budget as well. This is separate from body storage and is not a claim that
the provider's entire address space fits in 2 MiB. Use 16 KiB input/output record
content buffers plus upstream record/crypto overhead; do not dynamically grow
handshake buffers. The selected source reassembles TLS handshake messages in its
bounded input buffer, so oversized encoded certificate messages fail rather
than obtaining unbounded peer-directed storage. Retain the upstream maximum of
eight intermediate CAs in a verified chain. This depth limit alone is not the
allocation bound; the input-buffer and allocation caps apply during parsing.

The 2 MiB budget is agreed policy, not a measured fit. Root loading and controlled
handshakes must validate it before packaging. Cap exhaustion returns a resource
error; ordinary allocator failure remains distinguishable. Neither failure may
disable verification or silently increase the budget. If the agreed profile and
bundle do not fit, report measured demand for discussion before changing policy.

### Framing, cleanup and error reporting

Retain the HTTP parser, accepted statuses and immutable whole-response model.
A close-delimited HTTPS response completes only with authenticated TLS
`close_notify`. Underlying TCP EOF without it is TLS truncation. Authenticated
TLS EOF before fixed-length/chunked framing completes is incomplete HTTP input.
Completed fixed-length, chunked and no-body responses do not wait for peer
shutdown and do not require a later `close_notify` to validate retained bytes.

After response completion, attempt a local `close_notify` with at most 100 ms
of the remaining original deadline, then release the stream without waiting
for peer shutdown. Skip the attempt if no time remains. Shutdown/notification
failure after valid complete framing is diagnostic only, matching HTTP's
existing treatment of shutdown failure; a native handle-close failure still
fails the fetch. On failure, abort/release the stream and free partial state.
Preserve the first fetch failure over cleanup failures. Never publish partial
or unverified bytes.

Distinguish trust, name, date, TLS protocol/truncation, native transport/clock/
entropy/deadline, HTTP rejection and resource failures inside libhttp and provider
logs. Map certificate rejection to `CALL_DENIED`, protocol/truncation to `CALL_IO`,
and preserve native call statuses and existing HTTP mappings. TLS allocation-cap
exhaustion maps to `CALL_QUOTA`, ordinary allocation failure to `CALL_NO_MEMORY`,
and recognizable encoded-size limits to `CALL_FILE_TOO_LARGE`. Keep the final
HTTP status (or zero when absent) in OPEN's existing `provider_status`; add no ABI
fields solely for internal library codes. Logs may include a TLS error code and
verification flags, but should not dump response bodies, secrets or full URIs
with query strings.

### Publication and lifetime

Reuse libhttp and the provider bridge, with explicit HTTP/HTTPS modes of `httpfs`
and separate provider instances for the two schemes. Each rejects the other
scheme and owns independent body/TLS budgets. HTTPS has its own trust/authority
configuration; publication remains explicit and per session. Scheme discovery
must not depend on a successful network request.

Preserve the existing 63-snapshot bound and provider retirement contract:
removal/replacement stops new discovery, existing snapshots keep their original
provider alive, and the process exits after its OPEN export and all snapshots
retire. Replacing trust or a scheme binding does not mutate existing snapshots.

### Update checkpoint

Source and public-root pins were checked on 2026-09-28. Before merging the ports
recipe in task 2, recheck the selected LTS release/security advisories and root
snapshot; repeat the compile/link probe for any changed input. Record any pin
change and rationale in that PR. Thereafter review upstream security notices
when updating dependencies and review pins at milestone handoff; never silently
follow upstream main or fetch the moving public-root URL in ordinary builds.

Task 2 must turn the agreed client profile into configuration, implement the
missing/native hooks, validate actual root/handshake memory demand, and exercise
a controlled connection manually. The checked-in builder needs CMake added for
that build; inspect the deployed image before asking the owner to publish an
updated builder. This is a host build-tool change, not a compiler change. No
container publication, runtime port or handshake validation occurred in task 1.

## Focused PR tasks

- [x] **1. Pin/probe and settle the HTTPS contract.** Probe the selected Mbed TLS
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
