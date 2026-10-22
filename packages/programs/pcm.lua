-- pcm, a development tool from the Pyxis userland.
return {
  id = "pcm",
  variant = "1",
  kind = "program",
  depends = {},
  commands = { pcm = { path = "pcm.pxe" } },
  entries = {
    { build = "userspace-root/pcm.pxe", at = "pcm.pxe" },
  },
}
