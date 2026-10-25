# TLS failure-reason qualification

2026-10-25: parent `ac325191` (code base `7b558fdc`), userland
`81ee85f9b9b354ff4dfe8160b39c7e453e9e45da`. Existing builder
`pyxis-llvm23.1.3-3d69e83`; fresh SDK, affected programs (`httpfs`, `shell`, `cat`)
and current kernel build. No compiler-container rebuild or libc/status ABI change.

QEMU q35/KVM, host CPU, 4 CPUs/4 GiB, UTC RTC, modern virtio-net/rng, OVMF.
The disposable image combines that kernel/programs with the public 0.0.8 archive
and freshly built Remote helpers, not an exact whole-current-main image.
Public ABI headers have no difference between 0.0.8 and this base. Fixture-only
bootstrap grants HTTPS write authority for the POST failure check; stock HTTPS
root attenuation is unchanged. A local resolver maps `tls-fixture.test` to the
host fixture. A generated public test CA is added only to this image's trust;
no external network, paid service, real credentials or hardware action is used.

`python3 /shared/present/tls-reasons/qualify.py` runs the ordinary native commands
against local Python/OpenSSL certificate and TLS-policy fixtures. Results:

| Fixture | User-visible reason | Result |
| --- | --- | --- |
| Trusted valid certificate | `OK` body | Exit 0 |
| Untrusted self-signed issuer | `untrusted issuer or bad signature` | Exit 1 |
| Expired certificate | `expired` | Exit 1 |
| Future certificate | `not yet valid` | Exit 1 |
| Wrong DNS name | `name mismatch` | Exit 1 |
| TLS-1.0-only peer | `TLS peer alert: protocol_version (70)` | Exit 1 |
| TLS-1.2 peer restricted to a disjoint cipher | `TLS peer alert: handshake_failure (40)` | Exit 1 |
| Connection reset | `Connection reset` | Exit 1 |
| One-second prepared request against a stalled peer | `Connection timed out` | Exit 1 |

Failed shell input redirection and POST completion print the same untrusted
certificate detail. `bad=$(cat HTTPS-EXPIRED-URI)` binds a failed value;
`cat $bad` prints its retained expiry reason. A final valid GET returns `OK`:
the failure checks do not end the shared provider. Commands, completions, exact
messages and artifact identities remain under `/shared/present/tls-reasons/`.

This is QEMU correctness qualification, not a native result or performance
comparison. Revocation/signature flags, unknown-code fallback and other peer
alerts are mapped by source inspection; those cases were not independently
induced. A peer's generic alert cannot establish its private negotiation cause.
