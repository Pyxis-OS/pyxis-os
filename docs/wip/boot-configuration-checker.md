# Boot configuration checker

Status: **owner idea, 2026-10-06, deferred until the
[system layout](../userland/system-layout.md) milestone was done.** Not yet
agreed; nothing here authorizes code. In the [Asterism](spaces.md#asterism)
direction this checker is the validation half of **Polaris**, which later also
applies configuration changes to the Continuum supervisor.

A native shell command that reads the
[boot configuration](../userland/init.md#boot-configuration) and reports
problems without blocking anything, for use while editing
`system://config/boot.lua`: more than one network owner, invalid volumes, a
`start` outside a space's roots, CPUs that are not present.

Proposed:

- share parsing and validation with boot init, so the two cannot disagree;
- separate static checks from checks against this machine's CPUs and volumes,
  and say "not checked" where it lacks the authority to look;
- show the merged result: replaced entries, and which spaces would start.
