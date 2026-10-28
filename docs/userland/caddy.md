# Caddy on Pyxis

Caddy 2.11.7 is an optional native Go package. It uses the stock Caddy CLI and
standard module imports, with the exclusions below. It is absent from the boot
archive and default package set.

## Build and install

Build the [pinned Go cross-toolchain](../development/go-toolchain.md), then pack
Caddy for this system's ABI:

```sh
make caddy-package GO_PREFIX=/absolute/path/to/pyxis-go REPOSITORY=/absolute/path/to/repository
```

The recipe downloads only through the internal Go module proxy, verifies module
sums, vendors its pinned dependency graph and applies the recorded Pyxis patches.
There is no upstream fallback or new LLVM container requirement. The package
retains dependency notices, module inventory and compiler/source provenance.
Like the other package targets, it publishes an unsigned snapshot under
`REPOSITORY_NAME` (default `public`). Set that name to match the configured
repository, or set it empty when the existing
[repository signing workflow](../interfaces/packages.md) will publish it. The
build never signs with an implicit key. Install from that configured repository
with `pkg install caddy`.

## Authority and configuration

The chosen space must explicitly set `tcp_listen = true` in its boot configuration.
Every shipped space leaves it false. Installed systems edit
`system://config/boot.lua`; live builds supply an edited configuration through
`LIVE_CONFIG`. Reboot after changing the setting. See
[boot configuration](init.md#boot-configuration).
Caddy's bundle declares required TCP CONNECT and LISTEN, memory, native threads,
precise/sleeping clock, entropy and completion creation. Without required authority,
bundle admission refuses; even `caddy version` needs the declared grant profile.
The separate private LISTEN source is used only for explicit bundle declarations;
ordinary undeclaring children still receive CONNECT only.

The LISTEN factory is scoped to held interfaces, not configured port numbers.
Only a concrete configured IPv4 address and nonzero port can bind. Wildcard,
loopback, ephemeral ports and address reuse remain unsupported. A DHCP address
change invalidates the listener: stop and start Caddy with the new address.

Supply configuration, content and certificate files through the space's held
roots; prefer a read-only content/config root for the server. A path spelling
supplies no missing capability. The examples use `home://caddy/` and QEMU's
`10.0.2.15`; replace the address with the machine's actual configured IPv4.

```sh
export HOME=home://
caddy version
caddy list-modules
caddy validate --config home://caddy/Caddyfile --adapter caddyfile
caddy run --config home://caddy/Caddyfile --adapter caddyfile
```

The [example Caddyfile](../development/examples/caddy/Caddyfile), equivalent
[JSON](../development/examples/caddy/http.json) and
[page](../development/examples/caddy/site/index.html) serve a static site on port
18080. They explicitly disable the admin API, automatic HTTPS, config persistence
and HTTP/3; storage cleaning is explicitly disabled for this read-only profile. Use foreground `run`; `start`, admin reload/stop and autosave/resume
are outside this delivery. Change configuration by stopping and starting the
foreground process. First Ctrl+C asks Caddy to shut down gracefully; a second
Ctrl+C forces termination. Owned script/group cancellation remains forceful.

For supplied TLS, the [TLS example](../development/examples/caddy/tls.json)
loads a certificate and key from a held read-only root. Configure
an explicit HTTPS listener and keep automatic HTTPS disabled. Clients must trust
the certificate's issuer; no validation bypass or automatic trust installation
is provided. ACME issuance and durable account/certificate management follow
separately. The supplied HOME text names an already held root and grants no authority.
Any operation requiring writable application storage needs its own
explicitly held directory; no implicit host HOME/XDG tree is provided.

## Feature limits

The build uses `nobbolt,nobadger,nomysql,nopgx,nosshagentkms`. Their Smallstep
backends remain unsupported. Standard module registration does not qualify every
module: the local ACME server/PKI, Tailscale integration, external key stores,
subprocesses, watch/reload and background service management are outside this
profile. Dependency platform adapters return honest unsupported errors where
native equivalents are absent. Readline raw-state operations use Go's native
terminal snapshots. Password input, suspension and resize callbacks remain
unsupported, as does Smallstep's certificate-transparency X.509 system-root lookup;
Go's ordinary `crypto/x509` still uses the pinned archive CA bundle. HTTP/3
dependencies still compile; there is no
upstream HTTP/3 compile-out tag and QUIC service is not qualified here. The
examples disable unsupported TCP keepalive probes with `keepalive_interval -1s`;
HTTP keep-alive still permits multiple requests on one connection. Go's MIME
adapter supplies its builtin extension table, without an ambient Unix database.

Go networking retains its [native limits](../development/go-toolchain.md): IPv4,
scoped DNS over UDP, bounded native operations and shared transport budgets.
Kernel storage or metadata admission can refuse before the Go heap is exhausted.
No production-load, native-hardware or arbitrary-module claim follows from a
successful request. The [qualification record](../development/experiments/caddy/README.md) identifies
the checked commands, protocols, revisions and nested startup samples. Native
hardware is not qualified.
