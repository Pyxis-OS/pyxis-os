# net

`net` lists retained controllers, native interfaces and the selected system
network policy. It is read-only: it defines, selects, binds and configures
nothing. Use it to inspect the [network profile](../devices/networking.md#boot-configuration-and-use).

```text
net
net --help
```

## Authority

`net.pxb` is a [program bundle](program-bundles.md) declaring optional
`net_registry/read`. The shell supplies only its held registry READ to a
declaring bundle. Without it, `net` reports the missing grant and exits with
failure. A held clock READ adds the age of a DHCP report; it grants no
configuration or packet authority. The registry inventory and policy operations
need neither policy MANAGE nor writable interface facets.

## Output

Controller rows appear in ascending boot-local controller ID order, with driver,
MAC, carrier, preparation and binding status. Drivers are `virtio`, `rtl8111` or
`cdc-ncm`; an unreported identity or carrier appears as `unknown`. `BOUND` says
`yes` or `-`; the interface rows identify the actual association. Incomplete
controller discovery is reported explicitly; rerun `net` later. An empty
inventory prints `no network controllers found`.

The `Native interfaces:` section lists `lo` and configured `ethN`/`wlanN`
interfaces in ascending opaque interface ID order. Each row includes its name,
class, MTU, address epoch and route revision. Known controller/driver, MAC,
carrier, IPv4 address/prefix and gateway follow. Administrative binding,
readiness and authentication are separate observations; unknown authentication
is not a failed or successful Wi-Fi association assertion.

DHCP phase is the maintainer's last reported assertion, with age when clock
READ succeeds. It is not kernel-verified lease health. Address retirement clears
report validity; a stalled maintainer can otherwise leave an old report.

The policy line identifies its revision and independently selected default,
DNS, Remote, log and debug interface IDs. Zero means no selection. The final DNS
line shows the selected numeric or DHCP-contributed server, or `unavailable`.
An interface's own DHCP contribution does not choose system DNS. These snapshots
do not prove that a selected interface currently carries traffic.
