# System power overlay: parked follow-ups

Task 1 is implemented (#659, with the NVMe force choices from #710); see the
[power overlay reference](../kernel/power-overlay.md) for behavior, authority,
accepted decisions and emergency-coverage limits. The owner can press
Ctrl+Alt+Delete in any tab, even in a game with mouse lock, and shut down, reboot
or return with a click or the keyboard.

Open check: the installed-system flush has not been verified natively; do it with
the next stick update ([native results](../development/experiments/power-overlay/README.md#native-results)).

Parked, each needing an owner assignment:

- **Bounded output-lock waits in the presenter**, so a stuck writer drops frames
  instead of stopping input. This changes ordinary presentation and needs a
  decision first. Revisit: [power overlay limits](../technical-debt.md#power-overlay-limits).
- **Holding off power operations while the installer writes**: the existing
  [Battery-aware Update](later-os-directions.md#power-and-acpi) follow-up; see
  [technical debt](../technical-debt.md).
- **A live-image warning** that RAM volumes are lost on shutdown.
- **Other system-overlay entries** (lock screen, switch space) only with users
  and sessions.
