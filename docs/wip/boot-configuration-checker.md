# Boot configuration checker

Status: **shared static checker accepted within [installer task 4](installer-tui.md), 2026-10-13; implementation in draft.** The [system layout](../userland/system-layout.md)
milestone it waited for is done. The ordinary `config validate` bundle shares startup schemas and reports merged provenance/available CPU checks; [current checkpoint](../userland/configuration-editor.md). Live application remains deferred. In the [Asterism](spaces.md#asterism)
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
