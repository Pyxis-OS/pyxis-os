# Caddy qualification

2026-10-23; QEMU only. Source and raw captures remain under
`/shared/present/caddy/`. No native/PXE action, test framework or instrumentation
is merged.

## Revisions and configuration

- Parent runtime/image: `40fc9d4f18d3`, followed by documentation/publication
  wiring at `b837f8db`. Same kernel/network ABI as main `7b11fb43`.
- Userland #365 `21a41d38686641c2f52541c47098d65cbe27c065` during the
  functional runs; final linear rebase `045058bf7fca65a7c51b6174e9dc41d3f24452c3`
  adds merged pkg-upgrade prerequisites without changing the LISTEN implementation.
  ports #155 `1326e30243a7fbe3a8d8a0fd55b46e8dd8b4154a`, included in
  #156 functional source `38bee12ccaea004492125815b5d0ce937036a5b9`, then
  final `adb6d27994217edaebd62501bf7c523c6536fccf`. The final recipe isolates
  ambient Go workspace/proxy flags and reproduces the identical qualified P1F.
- Go #10 `d37622551cd6d68952d4c11d8c82b2772b1e1a8a`, based on merged
  networking `1780bcb4`. Existing compiler tools with this stdlib source;
  no LLVM container change or host ELF converter.
- Caddy v2.11.7, `72dd0fb067f6d7826c7f79907670ba4a713bfe37`, source ZIP
  SHA-256 `c5b3a3304999625d751b2d2284cb037b9530bf563d1a62b720a7aa6765b0a5d9`.
  Internal Go module proxy only; original `go.mod`/`go.sum` remain byte-identical
  across vendoring, then target build runs offline with recorded patches.
- Stock main/modules, `nobbolt,nobadger,nomysql,nopgx,nosshagentkms`, CGO off,
  amd64/v1, `-trimpath -ldflags='-s -w'`. Runtime P1F is 49,642,513 bytes,
  SHA-256 `daa73ffa86ca8e03b822d5f6bf6283813cf384bc4e858a840cf287ccce4c466f`.
  The package retains 146 compiled Go modules' inventory/notices; Caddy reports
  135 standard module registrations. Registration is not feature qualification.
- Four-vCPU nested KVM/Q35, `-cpu max,+invtsc`, 2 GiB, standard VGA, modern
  VirtIO net/RNG and read-only VirtIO-SCSI ISO. Invariant TSC is exposed to the
  host VM. No native disks. Caddy uses GOMAXPROCS=4 and explicitly supplied HOME.
  Shipped spaces stay unchanged; private qualification enables `tcp_listen` in
  Development/Remote, plus the existing jobs provisioner for the adapter callers.

## Build baseline and correction

The unchanged source on merged Go networking failed in six reached packages:
Caddy's POSIX traps, grpc, readline, tscert, logrus and certificate-transparency
X.509 platform selection. Narrow patches select real supported operations or
explicit unsupported results. Their database/agent exclusions remain exclusions.

The first linked static-file request faulted at `mime.initMime`: Go installed its
builtin table, then called a nil platform callback. The P1F's own pclntab and a
QEMU hardware breakpoint identified that call. Go #10 installs the missing Pyxis
initializer without searching ambient Unix databases. No loader-visible symbols
or broader VM change were needed. There is no functioning pre-port Caddy runtime
baseline to compare against.

Full image and actual optional `make caddy-package` build/pack passed. The
50,954,752-byte USTAR package was checked, published as a development unsigned
repository, installed with pkg and resolved by `which caddy` from package set 1.
The normal publication helper/signing controls apply; this fixture uses an
explicit unsigned local repository, not a release trust bypass. The final
main-based default core image also built and booted its rescue space, with no
Caddy or private qualification files in the archive.

## Functional results

| Check | Result |
| --- | --- |
| Shipped defaults, no LISTEN source | Bundle refused: missing required TCP resource. Private source unavailable as a URI root. |
| CLI | Version, 135-module inventory, JSON validation and Caddyfile adaptation/validation passed. |
| Static HTTP | Status 200, correct builtin HTML MIME and exact source-body hash; HTTP/1.0 and explicit Connection-close bodies also matched. |
| Concurrent HTTP | Twelve concurrent connections, four keep-alive requests each: all 48 complete bodies matched. |
| Supplied TLS | HTTP/1.1 and negotiated HTTP/2 returned exact bodies using the separately supplied test CA. Client without that CA refused, curl status 60. No ACME/trust installation. |
| Reverse proxy | Numeric native TCP upstream; exact host response body received. |
| Missing config root / duplicate bind | Explicit not-found / native queue-full errors, exit 1; no fallback config or replacement listener. |
| Shutdown | First Ctrl+C logged shutdown complete and exited 0; second press during a waiting shutdown terminated the process; fresh binds worked afterward. |
| Lua run/jobs | Raw children at most CONNECT; required private LISTEN child succeeds only from held authority. Explicit omitted/CONNECT-only selections refuse. |
| libuv | Private source absent from plain default inheritance; explicit CONNECT raw child passes; declared LISTEN bundle passes; explicit attenuation and missing parent authority refuse with UV_EACCES. |

Read-only supplied-certificate settings explicitly disable Caddy storage cleaning;
no simulated chmod/permission success. TCP keepalive probes are disabled because
the native option is unsupported; HTTP keep-alive remains enabled. Scripts and
owned scopes retain forceful group cancellation rather than cooperative first
Ctrl+C.

With **32 held HTTP keep-alive connections**, read-only debugger observation found
7 live/stored native tasks, 34 readiness registrations and 17,992 bytes of
readiness metadata. This is a snapshot, not a capacity bound or one-thread-per-
connection model. No debugger was attached for the startup samples below.

## Startup observation and limits

Three fresh processes from the warm RAM-backed installed package: host monotonic
command submission to a complete, hash-verified first HTTP response was
**226.9, 180.1, 179.1 ms**. Every response was 200 and subsequent graceful exit
was zero. These are nested KVM/Remote/NAT measurements, including launch and
request delivery; not cold boot, native hardware or a sustained throughput claim.

Native TCP Close semantics are unchanged; these completed responses do not prove
arbitrary large-transfer/early-close behavior. HTTP/3, ACME/account/certificate
storage, admin/reload/background lifecycle, arbitrary modules and native hardware
remain outside this qualification. See the [Caddy guide](../../../userland/caddy.md)
for the supplied authority and supported profile, and the
[gallery](../../../gallery/README.md) for the labeled guest/browser capture.
