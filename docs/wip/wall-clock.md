# UTC wall-clock and calendar conversion

Agreed next timekeeping milestone, following TTY horizontal tabs. This establishes
calendar time independently of timezone selection and Lua. The current
[monotonic clock](../timekeeping.md) remains the source for elapsed intervals,
sleep and timeout deadlines.

## Contract

- Kernel wall-clock time is based on UTC. Calendar adjustments must not change
  monotonic readings or outstanding deadlines.
- Initialize from the platform clock using the simpler existing x86/UEFI boot
  environment; PCI, VirtIO and virtio-rtc are not prerequisites.
- Userspace converts timestamps to calendar fields. Pyxis's own date display
  uses `YYYY-MM-DD` and 24-hour time; UTC timestamps identify UTC explicitly.
- Locale selection is deferred. Do not change specified C-library formatting
  contracts merely to make all C APIs produce Pyxis's preferred date notation.
- Reading time and converting dates never require a Lua runtime or config file.

## Focused tasks

- [ ] Agree on the UTC boot source, timestamp epoch/representation, range,
  precision, leap-second policy and behavior when the initial clock is invalid.
  Decide whether setting the clock belongs in this slice and, if so, its authority
  and adjustment behavior. Resolve these before implementing the interfaces.
- [ ] Implement UTC initialization and the native wall-clock read interface,
  retaining independent monotonic timing.
- [ ] Add the userspace UTC calendar conversion and C time facilities needed by
  real consumers, with a small date-display utility using the ISO convention.
- [ ] Enable TCC's deferred calendar macros and elapsed-time reporting using the
  appropriate wall/monotonic sources. Review its concrete requirements first.

Use ordinary builds, manual boots and debugger inspection. Keep implementation
PRs focused; this list does not authorize filling unrelated libc gaps.

Next: [boot archive assembly](boot-archive.md), then
[zoneinfo-backed local time](timezones.md). Lua configuration follows those.
