-- fat32ctl from the Pyxis userland.
return {
  id = "fat32ctl",
  variant = "1",
  kind = "program",
  depends = {},
  commands = { fat32ctl = { path = "fat32ctl.pxb", command = "fat32ctl" } },
  entries = {
    { build = "userspace-root/fat32ctl.pxb", at = "fat32ctl.pxb" },
  },
}
