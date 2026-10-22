-- date from the Pyxis userland.
return {
  id = "date",
  variant = "1",
  kind = "program",
  depends = {},
  commands = { date = { path = "date.pxe" } },
  entries = {
    { build = "userspace-root/date.pxe", at = "date.pxe" },
  },
}
