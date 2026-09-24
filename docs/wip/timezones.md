# Zoneinfo-backed local time

Agreed milestone after [UTC calendar support](../wall-clock.md) and
[boot archive assembly](../boot-archive.md), before guest Lua configuration.

## Contract

Use a pinned release of the [IANA timezone database](https://www.iana.org/time-zones)
and its standard compiled TZif files. Package the full zone database, including
its aliases, rather than a UTC/Bucharest subset. Keep IANA names such as
`Europe/Bucharest`; derive offsets and daylight-saving transitions from the data,
not handwritten Romanian rules. Materialize aliases as regular archive files
where needed by the current reader, without requiring kernel link support.

The initial default is `Europe/Bucharest`. The kernel clock remains UTC;
userspace handles timezone conversion. Pyxis's date convention remains
`YYYY-MM-DD` with 24-hour time, and full local timestamps include a numeric UTC
offset. Timezone selection does not introduce locale selection.

The timezone library works independently of Lua. Later configuration selects
the default zone; it does not implement conversion or become necessary for
clock reads. A possible packaged location is `app://share/zoneinfo/`; settle the
actual lookup and authority contract before implementation.

## Focused tasks

- [ ] Select the pinned data/compiler and conversion implementation; review
  licenses, TZif coverage including future transition rules, timestamp ranges,
  and invalid/missing-zone behavior before coding.
- [ ] Build and include the full database using the archive manifest, preserving
  provenance and avoiding dependence on the host's installed timezone version.
- [ ] Add userspace local-time conversion and date-display selection. Decide
  the interface for a default zone and explicit overrides, plus handling of
  ambiguous/nonexistent local times if reverse conversion is exposed.
- [ ] Verify UTC/local conversions and seasonal boundaries through ordinary
  userspace/debugger use, with the kernel's monotonic clock unchanged.

User/space-specific preferences and live configuration reload are separate
policy decisions. [Lua configuration](lua-port.md) will supply the first
system default through userspace init/session setup.
