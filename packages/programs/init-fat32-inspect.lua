-- init-fat32-inspect, a development tool from the Pyxis userland.
return {
  id = "init-fat32-inspect",
  variant = "1",
  kind = "program",
  depends = {},
  commands = { ["init-fat32-inspect"] = { path = "init-fat32-inspect.pxe" } },
  entries = {
    { build = "userspace-root/init-fat32-inspect.pxe", at = "init-fat32-inspect.pxe" },
  },
}
