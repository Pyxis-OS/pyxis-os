# net

`net` lists the network controllers the kernel retains and the state of net0.
It is read-only: it selects, binds and configures nothing. Use it to find which
adapter has link and its MAC, for example before choosing a controller for
[net0 selection](../devices/net0-selection.md).

```text
net
net --help
```

## Authority

`net.pxb` is a [program bundle](program-bundles.md) that declares an optional
`net_config` grant with the `read` right alone. The shell supplies its
[net0 configuration](../devices/net0-selection.md) NET_CONFIG grant, attenuated to READ, only to a
bundle that declares it; plain programs still receive none. Without the grant,
for example when the launching shell holds none, `net` prints
`net: no net_config grant` and exits with failure. It uses the controller
inventory and net0 query operations, which need no packet or configuration
authority.

## Output

One line per controller, in ascending ID order (MAC masked here):

```text
ID   DRIVER   MAC               LINK     PREPARED  BOUND
5    virtio   xx:xx:xx:xx:xx:xx up       yes       net0
net0: 10.0.2.15/24 gateway 10.0.2.2 dns 10.0.2.3
```

- **ID:** the boot-local controller ID, opaque and stable until reboot.
- **DRIVER:** `virtio`, `rtl8111` or `cdc-ncm`, as the kernel reports it.
- **MAC:** `unknown` when the controller reports no identity (an all-zero address).
- **LINK:** `up` or `down` when carrier is reported; `unknown` when it is not,
  including VirtIO without STATUS.
- **PREPARED:** whether the controller is prepared for use.
- **BOUND:** `net0` for the controller bound to the net0 interface, `-` otherwise.

The last line shows net0 as `unbound`, `bound, no IPv4 address assigned`, or the
address with prefix and gateway (`gateway none` when absent). The DNS server is
the one userspace chose and is shown in every case, `dns none` when unset.

When discovery has not finished, `net` says so after the list; run it again
later. With no controllers it prints `no network controllers found`.

## Qualification (2026-10-18)

Interactive QEMU (KVM, four CPUs, 2 GiB, VirtIO net) showed the VirtIO row bound
to net0 with its DHCP address, gateway and DNS, in the Development and Read-only
spaces. The `rtl8111` and `cdc-ncm` rows and the missing-grant message were
checked only by reading the code; the existing run options provide no USB CDC NCM
device (QEMU's `usb-net` is ECM/RNDIS).
