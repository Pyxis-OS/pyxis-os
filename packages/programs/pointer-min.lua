-- pointer-min, a development tool from the Pyxis userland.
return {
  id = "pointer-min",
  variant = "1",
  kind = "program",
  depends = {},
  commands = { ["pointer-min"] = { path = "pointer-min.pxe" } },
  entries = {
    { build = "userspace-root/pointer-min.pxe", at = "pointer-min.pxe" },
  },
}
