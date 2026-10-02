# lspci

The normal image includes native `lspci` at `app://lspci.pxe`. It lists the
kernel's boot PCI inventory through the
[system-information PCI queries](../interfaces/system-information.md#pci-inventory)
and resolves names from the packaged
[PCI ID database](../development/ports.md#pci-id-database).

```text
lspci
lspci -n
lspci -i host://pci.ids
lspci | head -n 3
```

## Output

Functions are sorted by address. Each line shows the address, class, vendor
and device, with numeric IDs always present. The revision is shown only when it
is nonzero:

```text
00:02.0 Ethernet controller [0200]: Red Hat, Inc. Virtio 1.0 network device [1af4:1041] (rev 01)
00:05.0 Unclassified device [00ff]: Device [1234:11e8] (rev 10)
```

The class label is the subclass name if the database has one, otherwise the
base-class name, otherwise `Class`. An unknown vendor name is left out, and an
unknown device name becomes `Device`. Database labels are descriptive text
only. Bytes outside printable ASCII are written as `\xHH`, and a backslash is
written as `\\`. Addresses carry a `SSSS:` segment prefix only if some function
has a nonzero segment. Today's kernel discovers segment zero only.

| Option | Effect |
| --- | --- |
| `-n` | Numeric only, as `BB:DD.F CCSS: VVVV:DDDD (rev RR)`; the database is not read |
| `-i FILE` | Read names from FILE instead of `app://share/hwdata/pci.ids` |

The database is read in one streaming pass. Vendor, device, class and subclass
lines are used. Subsystem and programming-interface lines are skipped, and
lines that don't match the `pci.ids` field layout give no name. If an ID is
listed more than once, the first label is used. The programming interface is
part of the query but is not shown.

## Exit status and diagnostics

| Condition | Output | Status |
| --- | --- | --- |
| Complete inventory | All functions | 0 |
| Database missing, unreadable or out of memory while loading | One stderr note, then numeric output for every function | 0 |
| Incomplete inventory | Retained functions, then a stderr warning | 1 |
| PCI unavailable | A stderr message | 1 |
| Missing `system_info`, query failure, bad usage or stdout failure | A stderr message | 1 |

A database failure discards any names read before it, so a partly read database
is never mistaken for missing entries.

## Authority and limits

`lspci` uses the `system_info` READ grant that local and remote shells already
pass to commands. It gets no configuration-space access, BAR or resource
information, driver ownership, reset authority or rescan. Subsystem IDs and
whether a kernel driver has claimed a function are deferred. `lspci` has no
verbose, tree, filter or machine-readable output mode.

## Validation evidence

Validation used Pyxis branch `hardware/lspci` with userland `0511b84` and
ports `0cf0a8a`, which pins pciids commit `75ba6ed`. The build was an
ordinary `make -j16 image` with the existing compiler. QEMU 10.2.2 ran under
nested KVM with four CPUs, 256 MiB, VirtIO NET/RNG and loopback TCP forwarding.
A QEMU wrapper added a PCIe root port with an xHCI controller behind it and the
`edu` test device. The inventory was 10 functions on 2 buses.

Both a quiet remote machine session and the local Development shell listed the
same 10 functions, sorted by address, with database names. They matched the
kernel discovery log and QEMU's `info pci`. Vendor `1234` (Bochs VGA and `edu`)
is absent from the pinned database and printed as `Device [1234:....]`. The
other output modes were checked over the remote session:

- `lspci -n` printed numeric output.
- `-i home://missing.ids` printed one note, fell back to numeric output and exited 0.
- An unknown option printed usage and exited 1.
- `lspci -n -i home://test.ids | cat` worked as a pipeline.

A small database written in the guest exercised:

- known vendors with unknown devices, and unknown classes and subclasses;
- a duplicate device label, where the first was used;
- skipped subsystem and programming-interface lines;
- malformed device lines, which gave no name;
- `\xHH` and `\\` escaping.

GDB showed 10 retained kernel records with class fields and revision, and a list
that ended after the tenth. Setting the kernel's completeness flag to false from
GDB produced the full list, the incomplete warning and status 1. Setting it to
unavailable produced the unavailable message and status 1. Both values were then
restored, and output was normal again. These two states were forced from the
debugger; no topology fault occurred. Inventory states caused by real
malformed topology or failed record allocation were reviewed in source only.
QEMU and GDB were stopped afterwards.
