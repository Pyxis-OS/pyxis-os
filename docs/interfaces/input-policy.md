# Boot input policy

Stock `boot://boot-init.pxe` receives the named `input_policy` object, with
`INSTALL_ONCE` authority. Installer and ordinary programs do not receive it;
boot init keeps it out of its forwarded service table. There is no query, live
setter, device access or contact-stream grant.

[`INPUT_POLICY_INSTALL_ONCE`](../../include/abi/input_policy.h) supplies two
validated direction enums, mouse and touchpad, a signed speed in thousandths
of [-1, 1] (default 0), and a required-zero reserved field. The request is 32
bytes. Invalid messages do not consume
the opportunity. The first successful installation consumes it until reboot;
another attempt returns `CALL_DENIED`. BSP input coordination owns the policy,
with natural defaults before installation, and applies it once to both axes of
each source. Source conversion fractions reset on installation.

Boot init validates archive `input.mouse_scroll_direction` and
`input.touchpad_scroll_direction`, each `natural` or `traditional`. Both shipped
archives use natural. Installed boots read these ordinary keys:

- `system://config/machine/input/mouse/scroll-direction`
- `system://config/machine/input/touchpad/scroll-direction`

The shared machine-settings schema accepts the exact enum; the bounded FILE
reader permits one optional final LF. Extra bytes, CR, NUL, whitespace or other
values refuse. Missing, unreadable or invalid installed keys retain that axis's
archive-authored default. `input.diagnostics = true` opts into fallback reports;
normal boots add none. Live boots use archive values. Pool `config/boot.lua`
cannot override these machine direction defaults. Its optional
`input = { touchpad_speed = 0 }` accepts a finite number in [-1, 1]; omitted
speed uses the archive value. Only a successfully applied override supplies it.
The pool input table accepts no direction/diagnostics keys. Speed applies to
native HID touchpads; Synaptics and separate mice retain their motion behavior.
Boot init installs before normal or
rescue spaces start. Changes apply at the next boot; ordinary file durability
and the [machine store convention](../userland/machine-settings.md) apply.

The validating writer and runtime settings tools remain separate work.
