# TLS failure reasons

Scope: user-visible request failures for 0.0.9. Proposal only; implementation
waits for the reporting-layer decision below.

## Inspected starting point

At parent `7b558fdc` / userland `a2d131a9`, libtls records the error class,
native status, Mbed TLS code and verification flags in `tls_result`.
`libtls/tls.c:handshake_transfer` collects certificate verification flags;
`httpfs/provider.c:transaction_description` currently falls back to
“Transaction failed” when no redirect-specific detail exists.

`libhttp/fetch.c:tls_status` already maps certificate/trust failures to
`CALL_DENIED`, protocol/truncation failures to `CALL_IO`, and preserves available
native transport/deadline statuses. Keep this mapping and the existing
[resource-outcome/errno translation](../userland/stdio.md); no new statuses.

`cat/main.c` already calls `pyxis_fopen` and owns its failed result, but prints
only `strerror` after releasing it. Shell redirection and transaction commit
also receive resource descriptions. Ordinary libc `fopen` intentionally releases
provisional failure grants; its opt-in result API retains them without a hidden
last-error slot. See [describe](../interfaces/file-providers.md#describe-and-error-body-contract).

## Proposed implementation

- Format request failures into the existing 255-byte provider description.
  Explicitly interpret named verification flags and Mbed TLS errors; retain an
  unmapped raw code. Distinguish transport timeout/reset from TLS negotiation.
- Report only facts available locally or in a peer alert. A generic handshake
  alert does not establish whether cipher, signature or group negotiation failed;
  name the actual alert when available rather than guessing the peer's cause.
- Preserve bounded descriptions and captured failures through existing ownership
  rules; no re-OPEN, provider stderr scraping, URI-specific libc dispatch or
  peer certificate/header dumps. Existing detail restrictions remain effective.
- Qualify in QEMU against local certificate and negotiation fixtures: untrusted
  issuer, expired certificate, name mismatch, TLS-1.0-only and disjoint ciphers.
  Record actual messages and their limits. Error-path work does not change the
  successful transfer loop; no throughput benchmark is proposed.
- Userland PR first, then a parent pin/docs PR. No kernel, ports or container
  changes are expected. Fold this note into the implemented HTTP/stdio references
  when delivered.

## Decision for chronium

**1. Reporting layer — proposed default:** first-party `cat` prints the retained
provider detail on failed OPEN/read; the shell does the same for failed resource
redirection and transaction completion, before releasing the result. Preserve
capture/handoff behavior: a failed value can still carry its description to its
consumer. Standard `fopen`, `errno`, `strerror` and `perror` stay quiet/static;
arbitrary programs must opt into `pyxis_fopen`/`pyxis_describe` to show detail.
There is no automatic stderr output from libc and no process/thread last-error
string. Example: `cat: https://example.invalid/: certificate expired` (possibly
with the existing coarse error prefix). Exact wording is diagnostic text, not an
API. This meets ordinary shell use without changing every port's error handler.
