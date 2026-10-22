-- threadprobe, a development tool from the Pyxis userland.
return {
  id = "threadprobe",
  variant = "1",
  kind = "program",
  depends = {},
  commands = { threadprobe = { path = "threadprobe.pxe" } },
  entries = {
    { build = "userspace-root/threadprobe.pxe", at = "threadprobe.pxe" },
  },
}
