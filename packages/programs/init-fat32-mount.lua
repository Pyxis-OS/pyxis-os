-- init-fat32-mount, a development tool from the Pyxis userland.
return {
  id = "init-fat32-mount",
  variant = "1",
  kind = "program",
  depends = {},
  commands = { ["init-fat32-mount"] = { path = "init-fat32-mount.pxe" } },
  entries = {
    { build = "userspace-root/init-fat32-mount.pxe", at = "init-fat32-mount.pxe" },
  },
}
