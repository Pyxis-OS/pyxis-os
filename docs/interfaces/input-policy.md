# Boot input policy

Stock `boot://boot-init.pxe` receives the named `input_policy` object, with
`INSTALL_ONCE` authority. Installer and ordinary programs do not receive it;
boot init keeps it out of its forwarded service table. There is no query, live
setter, device access or contact-stream grant.

[`INPUT_POLICY_INSTALL_ONCE`](../../include/abi/input_policy.h) supplies two
validated direction enums, mouse and touchpad, a signed speed in thousandths
of [-1, 1] (default 0), and the initial Num Lock state, 0 or 1 (default 0). The
request is 32 bytes. Invalid messages do not consume
the opportunity. The first successful installation consumes it until reboot;
another attempt returns `CALL_DENIED`. BSP input coordination owns the policy,
with natural defaults before installation, and applies it once to both axes of
each source. Source conversion fractions reset on installation. Installation sets
Num Lock to the requested state and the keyboard lights follow it, so a keyboard
whose firmware lit Num Lock at power-on is corrected even when the setting is off.

Boot init validates archive `input.mouse_scroll_direction` and
`input.touchpad_scroll_direction`, each `natural` or `traditional`. Both shipped
archives use natural. Installed boots read these ordinary keys:

- `system://config/machine/input/mouse/scroll-direction`
- `system://config/machine/input/touchpad/scroll-direction`

The shared machine-settings schema accepts the exact enum; the bounded FILE
reader permits one optional final LF. Extra bytes, CR, NUL, whitespace or other
values refuse. Missing, unreadable or invalid installed keys retain that axis's
default from the boot configuration: `boot://config/live.lua` on live boots,
the pool's `system://config/boot.lua` on installed ones. `input.diagnostics =
true` opts into fallback reports; normal boots add none. The same `input` table
sets `touchpad_speed`, a finite number in [-1, 1] (default 0), and `num_lock`,
a boolean (default off). Speed applies to native HID touchpads;
Synaptics and separate mice retain their motion behavior. `num_lock` is the
state at boot, not a lock: pressing Num Lock still toggles it.
Boot init installs before normal or
rescue spaces start. Changes apply at the next boot; ordinary file durability
and the [machine store convention](../userland/machine-settings.md) apply.

The validating writer and runtime settings tools remain separate work.
