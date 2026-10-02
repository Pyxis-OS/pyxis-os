# Native hardware inspection and text databases

Status: agreed direction, 2026-10-02. The PCI database and `lspci` are
implemented. `usb.ids` packaging and `lsusb` remain unassigned, and USB
inventory authority needs discussion before code.

Provide native `lspci` and `lsusb` tools with packaged, plain-text name databases.
PCI discovery already exists, so `lspci` can be assigned independently; `lsusb`
follows [USB enumeration](usb-installation.md#b-native-read-only-usb-storage) and
should help inspect devices even when no class driver supports them. These tasks
do not expand [boot-image Phase A](usb-installation.md#a-bootable-usb-image).

## Tools and observation boundary

- `lspci`: PCI address, numeric vendor/device IDs, resolved names, class and
  driver ownership where the kernel exposes it.
- `lsusb`: controller/port topology, numeric IDs, device-provided strings and
  resolved names, negotiated speed, interface classes and bound/unsupported state.
  Endpoint detail is useful for debugging once that information exists.

Expose observed inventory through explicit read-only observation authority.
Listing must not grant raw configuration access, reset authority, arbitrary USB
transfers or access to storage contents. Settle the inventory scope, lifetime and
delegation contract before adding an ABI; reuse existing observation facilities
where appropriate. Userland owns name lookup and formatting. Device strings and
database labels are descriptive data, never authentication, grants or driver
selection policy; escape device text for terminal output.

### Agreed PCI observation

Agreed and implemented on 2026-10-02:

- PCI inventory uses two new queries under the existing `system_info` READ
  right. There is no new right or grant, so local and remote commands that
  already receive READ can list PCI devices.
- One query reports the inventory state (unavailable, incomplete or complete)
  and the count. The other returns one function by index, with address, IDs,
  class, programming interface, revision and header type. The kernel keeps these
  values from discovery and never re-reads configuration space for them.
- Subsystem IDs and a kernel-driver-claimed flag are deferred. The kernel keeps
  only a claim pointer, not a driver name.
- `lspci` exit policy: a missing database gives numeric output and status 0, an
  incomplete inventory lists functions with status 1, and an unavailable
  inventory is an error with status 1. Options are `-n` and `-i FILE`.

See [system information](../interfaces/system-information.md#pci-inventory) and
[lspci](../userland/lspci.md). USB serial strings may justify a separate right;
decide that with `lsusb`.

## Packaged databases

Package pinned upstream snapshots, preserving provenance and license notices,
at proposed paths `app://share/hwdata/pci.ids` and
`app://share/hwdata/usb.ids`. Both databases offer BSD-3-Clause or GPL-2.0-or-later;
use the BSD option. No udev hardware database, libusb port, full pciutils port or
background updater is required by these native tools. Missing database entries
must leave numeric identification usable, with device strings where available.

Downloaded source observations on 2026-10-02:

| Database | Snapshot date | Uncompressed bytes | Lines |
| --- | --- | ---: | ---: |
| `usb.ids` | 2026-06-26 | 730,605 | 25,705 |
| `pci.ids` | 2026-10-01 | 1,671,363 | 43,261 |

Together these are about 2.3 MiB of useful text for hardware naming, editor use,
buffered reads and future `wc`, `grep`, `sort` and pipeline workloads. These
observations select no permanent version or size. Pin the chosen revisions when
packaging; database updates must not break validation through frozen line counts,
specific vendor entries or copies of today's contents. Use small independently
defined inputs for parser behaviour; the real corpus is ordinary packaged data.

The [everyday pipeline target](../development/io-reliability-attribution.md#everyday-pipeline-performance-target)
records a proposed use of the PCI database for `cat | wc`, independently of these
tools and USB implementation.

## Focused tasks

1. [ ] **Package the two text databases.** Choose immutable upstream revisions,
   preserve BSD notices and stage through existing asset assembly. Keep the
   files readable by ordinary text tools; no compiled database is needed initially.
   `pci.ids` is packaged by the [pciids recipe](../development/ports.md#pci-id-database);
   `usb.ids` remains.
2. [x] **Expose PCI inventory and implement native `lspci`.** Settle observation
   authority first, then use existing enumeration without rescanning hardware
   from userland. Exercise known and unknown IDs and missing name data.
3. [ ] **Expose USB inventory and implement native `lsusb`.** Follow enumeration,
   showing supported and unbound devices through the same observation boundary.
   Validate topology and interface reporting alongside the selected storage device.

## References

- [USB ID database and licensing](https://usb-ids.gowdy.us/) and its
  [text source](https://github.com/usbids/usbids).
- [PCI ID database and licensing](https://pci-ids.ucw.cz/) and its
  [text source](https://github.com/pciutils/pciids).
