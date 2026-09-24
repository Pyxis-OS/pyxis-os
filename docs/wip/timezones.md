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

Absent `TZ` defaults to UTC. `TZ=Europe/Bucharest` explicitly selects Bucharest;
later configuration may supply that preference. The kernel clock remains UTC;
userspace handles timezone conversion. Pyxis's date convention remains
`YYYY-MM-DD` with 24-hour time, and full local timestamps include a numeric UTC
offset. Timezone selection does not introduce locale selection.

The timezone library works independently of Lua. Later configuration selects
the default zone; it does not implement conversion or become necessary for
clock reads. The packaged location is `app://share/zoneinfo/`, reached through
the process's existing `app` directory capability. `TZ` selects an IANA zone name. Invalid or
inaccessible named zones report an error rather than silently falling back.
`date` will display local time with its numeric UTC offset; `date -u` will select
UTC. Reverse conversion (`mktime`), ambiguous/nonexistent local-time input and
locale support are outside this slice.

## Focused tasks

- [x] Pin IANA tzdb/tzcode 2026d and its matching host compiler, review licenses
  and compile the standard database with all aliases. Use slim TZif, no range
  cutoff and no leap-second table, matching Pyxis's Unix-second clock.
- [x] Include the full standard database through the ports install manifest,
  with aliases as independent regular files, provenance and license notices.
  See [packaged timezone data](../timezone-data.md).
- [ ] Select and audit the conversion implementation. Review TZif bounds,
  64-bit transition handling, future-rule footers, timestamp overflow and
  invalid/missing-zone errors before coding. The library must work without Lua.
- [ ] Add userspace local-time conversion and date selection with the above
  default/override policy. Decide the concrete C API and empty-TZ behavior
  before implementing the runtime. No reverse conversion in this milestone.
- [ ] Verify UTC/local conversions and seasonal boundaries through ordinary
  userspace/debugger use, with the kernel's monotonic clock unchanged.

User/space-specific preferences and live configuration reload are separate
policy decisions. [Lua configuration](lua-port.md) will supply the first
system default through userspace init/session setup.
