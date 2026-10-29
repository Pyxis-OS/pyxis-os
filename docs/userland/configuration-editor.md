# Checking the installed configuration

An installed system's configuration is three files on the pool:
`system://config/boot.lua`, `session.lua` and `network.lua`
([installed configuration](init.md#installed-configuration)). Edit them
directly; they apply at the next boot. Boot applies none of them unless all
three are valid, and otherwise starts the rescue space. The optional
[space editor](spaces.md) edits the spaces in `boot.lua`.

`config validate` checks them the way boot reads them, as literal data:

- `boot.lua`: the volumes and spaces it defines, listed by name with each
  space's init, and each space's CPUs against this machine when `system_info`
  is readable;
- `session.lua` and `network.lua`: their schemas.

It ends with whether boot would apply the configuration, and exits nonzero
when it would not. Mounted volumes, packaged timezone data and live network
availability are reported as not checked.

The ESP holds no configuration. Until task 2b of
[package upgrades](../wip/pkg-upgrade.md) the installer also published an ESP
copy with a revision catalog, and `config plan`, `edit` and `apply` edited it;
both are gone.
