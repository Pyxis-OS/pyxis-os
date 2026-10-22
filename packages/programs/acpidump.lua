-- acpidump from the Pyxis userland.
return {
  id = "acpidump",
  variant = "1",
  kind = "program",
  depends = {},
  commands = { acpidump = { path = "acpidump.pxe" } },
  entries = {
    { build = "userspace-root/acpidump.pxe", at = "acpidump.pxe" },
  },
}
