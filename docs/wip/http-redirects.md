# HTTP redirects proposal

**All three defaults accepted 2026-10-09; implementation assigned.**
Deferred 2026-10-04 (“wanted, not yet”); [Links](../userland/links.md) hits redirects
on ordinary sites.

## Accepted decisions (2026-10-09)

1. **Follow policy: 301/302/303/307/308, at most ten redirects**
   (eleven requests including the original), with visited-URL detection. This
   covers ordinary moves without choosing a 300 representation or inventing a
   304 cache.
2. **Transport/trust: allow HTTP→HTTPS and public-root cross-origin
   redirects; reject HTTPS→HTTP and origin changes involving custom trust.**
   Use only delegated scheme providers, rebuild request headers for each hop,
   and never transfer custom roots or credentials.
3. **Ownership/consumer contract: shared userspace open bridge owns
   the chain, with copied response metadata available from FILE.** Preserve one
   30 s deadline and a 16 MiB body budget across hops; Links consumes the final
   URL before interpreting the page. The contract below governs implementation.

## Following and resolving

Final success remains complete 200/204; unselected/unknown 3xx remain errors.
Require one nonempty valid `Location`, at most 2,048 bytes; resolve against the
**current hop**, including relative, query-only and scheme-relative references.
Resolved URLs also fit 2,048 bytes and the current ASCII/DNS/IPv4/port/escape
subset; HTTPS still requires DNS names. Reject non-HTTP(S) schemes. Preserve
encoded path/query except dot-segment resolution; inherit an omitted fragment,
but never transmit it.

| Code | Method/body policy |
| --- | --- |
| 301, 302, 303 | GET, no body; remove body-specific headers when converting. |
| 307, 308 | Preserve method and exact body; reject if it cannot be replayed safely. |

Delivery stays **GET only**; the table does not authorize POST/body replay.
301/302 conversion is Pyxis policy, not an RFC requirement for all methods.
No network retries, bridge/provider redirect cache or HTML/Refresh redirects.

Before contacting any target, compare it with the initial URL and all visited
targets. Keys lowercase scheme/DNS host, remove a terminal DNS dot, use effective
ports and `/` for empty
paths, normalize escapes (decode only unreserved characters) then dot segments,
preserve query order, and ignore fragments. Comparison does not rewrite outgoing
escaped bytes. Reject repeated keys, including self/fragment-only cycles. DNS
aliases are not merged; the hop cap bounds aliases and changing-query chains.

[HTTP semantics](https://www.rfc-editor.org/rfc/rfc9110.html#section-15.4),
[Location and fragment inheritance](https://www.rfc-editor.org/rfc/rfc9110.html#section-10.2.2)
and [URI resolution](https://www.rfc-editor.org/rfc/rfc3986.html#section-5.2)
are the references; the selected limits and exclusions are Pyxis policy.

## Authority and trust at every hop

An origin is scheme, lowercase host without a terminal DNS dot, and effective
port. The bridge uses only the caller's path context/namespace and retains each
scheme binding on first use until completion/failure, preventing replacement
mid-chain.
Missing HTTPS authority fails an upgrade. Each target must select a provider,
never a native root named http/https; ambiguous root/provider bindings fail before
local traversal. Neither HTTP nor TLS failure creates authority or a binding.

Public-root HTTPS verifies the new hostname/SNI and certificate at each hop.
Custom-CA instances keep their frozen public+custom union only for an initial
HTTPS request and a same-origin chain. **Any crossing makes custom trust
ineligible for the remainder of the chain**, including A→B→A. OPEN carries the
initial origin and sticky crossing state; the provider checks **before DNS/TLS**,
also refusing HTTP→HTTPS when its HTTPS binding has custom roots. Do not drop
custom roots, transfer them or create hidden public-only runtimes. This rejects
even publicly signed targets through a custom instance; broader trust needs
another decision. It is continuity policy, not a destination sandbox: OPEN
holders can still choose independent initial URLs.

Regenerate Host, Connection: close and Accept-Encoding: identity. No cookie,
Authorization or custom-header API exists; if added later, drop all supplied
headers/credentials on origin change. Credential URLs remain invalid; no Referer.

## Bridge, snapshot bounds and final URL

[Providers](../userland/http-fetch.md) remain separate one-hop services without
caller namespace authority; HTTP has no TLS runtime. Extend OPEN with a distinct
**REDIRECT outcome**: bounded Location, HTTP status/accounting, **no FILE grant**.
Validate headers, abort/close without draining the intermediate body, release
staging/TLS, then reply. The bridge validates layout/grants before following;
BYTES success still owns exactly one FILE. Malformed replies close all grants.
Share transport-neutral URI helpers through libpyxis without TLS/parser linkage.
Use bounded caller-owned redirect scratch independent of initial path length;
libc allocates before first OPEN and frees after completion/failure. Low-level
path helpers remain allocation-free.
Update in-tree callers together; native traversal and kernel behavior stay intact.

Before URI processing/lookup, use explicit caller clock READ authority for
min(now + 30 s, earlier deadline), shared by every OPEN/DNS/TLS/cleanup phase.
Missing clock fails HTTP(S) before OPEN; native files are unaffected. Preserve
DNS attempt bounds and the 2 MiB TLS instance cap; transport failures never replay.

Carry remaining budgets in each hop request and consumed counts in its reply;
enforce them inside libhttp before receiving/allocating, not after a hop completes:
**16 MiB total body allowance**, **32 KiB headers/trailers and 256 fields across
the chain**, and existing chunk/informational bounds. Charge discarded HTTP
body read-ahead from redirect headers conservatively as body bytes; do not read
an intermediate body to completion. Final decoded body must fit the remainder,
not receive a fresh 16 MiB allowance. Framing overhead retains its existing
separate bound. The 64 MiB per-provider body-storage quota still counts retained
snapshots and growth overlap; the chain retains no intermediate snapshot or
body allocation. A declared but unread redirect body is not allocated or charged.
Redirect success means validated headers and bounded Location plus checked
stream closure, not a completely received intermediate body. All failure paths
release temporary grants/storage and expose no partial FILE. Loop/hop exhaustion
returns CALL_LIMIT; invalid Location/outcomes CALL_BAD_REQUEST; downgrade or trust
refusal CALL_DENIED; body/header excess CALL_FILE_TOO_LARGE. Existing transport,
binding and final-status errors retain their mapping.

Retain final absolute URL (including fragment), redirect count, final status
and media type with the descriptor/FILE. The copy-out
`pyxis_stdio_response(FILE *, struct pyxis_response_info *)` in `<pyxis/stdio.h>`
returns presence flags, status/count, a 2,049-byte URL and 128-byte media type.
Valid local/inherited streams report absence; invalid/closed streams fail.
No position/indicator/authority changes; close frees metadata. Native path callers
can request the same data; byte-only callers keep their interface. Failed fopen
still reports errno; native diagnostics retain the last parsed HTTP status.

Links copies metadata before fclose and adopts the final URL for base/relative
links, fragment position, address/history, downloads and cache identity. Keep
requested URLs as aliases for its cached page snapshot: adapt upstream redirect
bookkeeping, which otherwise calls load_url again; never rewrite its inline
cache-tree URL key. Prefer Content-Type, retaining sniff/extension fallback when
absent. Local FILEs stay unchanged; representation negotiation, authentication,
cookies and async browsing remain separate.

## Delivery after acceptance

1. [ ] Implement shared URI resolution, OPEN redirect/context/accounting and
   bridge ownership in Pyxis ABI + userland; update in-tree providers together.
2. [ ] Retain response metadata in libc and adapt Links in ports; publish the
   dependency PRs before parent pins, with explicit merge order.
3. [ ] Capture matched direct-GET baseline/after runs, then manually qualify
   relative resolution, all five codes, cycles/hop limit, upgrades/downgrade,
   custom-trust refusal before target traffic, budgets/deadlines, cleanup and
   Links base/history/Back. Use controlled HTTP/TLS peers and ordinary QEMU;
   no new test/CI infrastructure. Close into the references when delivered.

The proposal PR records acceptance; implementation and qualification are delivered
in separate userland, ports and Pyxis integration PRs. The owner reviews/merges.
