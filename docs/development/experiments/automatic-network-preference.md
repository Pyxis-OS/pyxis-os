# Automatic network preference qualification

## Revisions and configuration

The pre-edit baseline is Pyxis `cd1f294ee9b67dcbffbec0e06f3892053192e792`
with userland `1a7722449d93a5ea56b7d070e948560415abc961`. The measured candidate is
Pyxis `d96ae51d2090d9be31081df6f2a53d5be34f26de` with userland
`c3be6fe69cd6a252955cebea7e6023423aa8ffb3`. Filesystem, ports and lwIP pins
are unchanged. The builder is
`git.internal/pyxisos/pyxis-builder:pyxis-llvm23.1.3-3d69e83`.
Both images were built cleanly; the candidate's complete kernel/SDK/ports/userland
source build passed. Later kernel/profile rebuilds reused matching components
under the [bundle rules](../build-bundles.md).

Functional guests used QEMU 10.2.2, Q35/KVM, host CPU, four CPUs, 2 GiB and the
Fedora 44 OVMF fixture, with fresh variables and two or three modern VirtIO NICs.
Independent slirp networks and a separately pinned Remote interface allowed data
carrier changes without replacing management. Temporary TCC clients observed
native route/endpoint results; a trusted qualification init held only declared
configuration facets for manual REPLACE/CLEAR/DHCP-report operations.
Raw captures, commands, snapshots and build logs remain under
`/shared/present/network-preference/`.

## Functional evidence

- Both automatic DHCP clients became usable. New default routes selected the
  initial winner; a narrower one-interface scope selected its permitted peer,
  and an outside-scope derivation was denied. Actual numeric DNS queries used
  the selected source interface, confirmed in QEMU packet captures.
- Data carrier down selected another usable peer for new routes while management
  stayed pinned. Carrier return retained the equal-rate winner. Equal connected
  prefixes behaved likewise; a longer prefix still won.
- Address replacement invalidated a held UDP endpoint without changing its
  retained interface/epoch. Changing the connected network away and back did not
  resurrect its previous tie winner; a host-address change within the same subnet
  and same-address renewal preserved current preference. DHCP failed/expired and
  configuration CLEAR excluded the affected path. Restoring it preserved equal
  ties. With no usable gateway, new off-link operations returned `NO_ROUTE`;
  connected routing and pinned management remained independent.
- Explicit route and numeric DNS pins returned `UNAVAILABLE` when their selected
  NIC lost carrier, despite usable peers, and recovered on carrier return.
  Named DHCP DNS remained available through its selected interface.
- Idle GDB inspection confirmed separate stable default/connected orders and
  connected-network epochs independent of host-address epochs.

An isolated Linux 6.19.10 gadget guest supplied real NCM notifications to a nested
Pyxis guest. Public controller/interface snapshots reported the synthetic
3,750,000,000 bit/s pair, cleared validity on DOWN and restored the pair on UP;
management IDs did not change. This qualifies notification propagation, not a
physical negotiated Ethernet rate. Its independent VirtIO never acquired IPv4,
including one isolated TAP retry, so known-speed NCM versus a simultaneously
usable unknown-speed peer was **not qualified**. The nested VirtIO readiness cause
is unassigned; NCM as the only usable default is not evidence for that comparison.
RTL8111 PHY bit decoding was source-inspected, not exercised on hardware. AX200
is scan-only and reports unknown; no Wi-Fi association/rate claim is made.

## Interleaved controls

Six sequential A/B boot pairs used the stock two-NIC configuration, with no packet
capture or debugger. Each boot warmed 16 `echo -n` launches, then repeated three
cycles of:

```lua
for i=1,1024 do assert(pyxis.run{"echo","-n"}==0) end
```

```sh
ttcp -t -n 2048 -l 8192 -p 15001 10.0.3.2
ttcp -r -p 15002 10.0.3.2
```

The localhost peers consumed/supplied exactly 16 MiB. TX includes closure; RX
runs to peer EOF. Launch time is host wall time including Lua, child launch/wait
and Remote completion. `fastfetch --structure Memory --logo none` sampled total
used backing before/after the workloads, not process RSS. There are 18 successful
samples per arm for time/throughput and six for each memory point.

| Metric | A median (min–max) | B median (min–max) | Median change |
| --- | --- | --- | --- |
| 1,024 launches, seconds | 2.741 (2.651–3.447) | 2.754 (2.653–3.058) | +0.5% |
| TX, MiB/s | 3.745 (2.463–3.828) | 3.739 (3.625–3.853) | −0.1% |
| RX, MiB/s | 6.009 (5.293–6.117) | 5.907 (5.128–6.163) | −1.7% |
| Used backing before, MiB | 65.11 (65.11–65.11) | 64.88 (64.88–65.13) | −0.23 MiB |
| Used backing after, MiB | 77.32 (77.32–77.32) | 77.08 (77.08–77.33) | −0.24 MiB |

An earlier candidate unnecessarily sampled VirtIO carrier configuration MMIO on
each preference refresh merely to obtain an unsupported speed. Its repeated
controls regressed TX/RX; the final rate-only helper returns unknown for VirtIO
without that extra polling. Those earlier raw records, including the stopped
partial run, are retained and excluded from the table. After the correction, an
initial nine-sample RX difference of −4.4% prompted three more interleaved pairs.
The final difference is −1.7%; B's paired RX median is higher in three pairs and
lower in three, with overlapping ranges. This does not establish zero overhead
or attribute remaining variation. The small backing difference is an aggregate
allocator observation, not evidence that interface state costs less memory.

These are nested-host QEMU controls, not native NIC bandwidth. No native/PXE
operation was performed. Physical rate changes, known-versus-unknown speed
ranking with both paths usable and native independent-management failover remain
unqualified; see [lifecycle debt](../../technical-debt.md#multiple-interface-lifecycle-qualification).
