-- fat32-inspect, a development tool from the Pyxis userland.
return {
  id = "fat32-inspect",
  variant = "1",
  kind = "program",
  depends = {},
  commands = { ["fat32-inspect"] = { path = "fat32-inspect.pxe" } },
  entries = {
    { build = "userspace-root/fat32-inspect.pxe", at = "fat32-inspect.pxe" },
  },
}
