-- bootmgr from the Pyxis userland.
return {
  id = "bootmgr",
  variant = "1",
  kind = "program",
  depends = {},
  commands = { bootmgr = { path = "bootmgr.pxb", command = "bootmgr" } },
  entries = {
    { build = "userspace-root/bootmgr.pxb", at = "bootmgr.pxb" },
  },
}
