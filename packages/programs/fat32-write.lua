-- fat32-write, a development tool from the Pyxis userland.
return {
  id = "fat32-write",
  variant = "1",
  kind = "program",
  depends = {},
  commands = { ["fat32-write"] = { path = "fat32-write.pxe" } },
  entries = {
    { build = "userspace-root/fat32-write.pxe", at = "fat32-write.pxe" },
  },
}
