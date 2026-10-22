-- font-min, a development tool from the Pyxis userland.
return {
  id = "font-min",
  variant = "1",
  kind = "program",
  depends = {},
  commands = { ["font-min"] = { path = "font-min.pxb", command = "font-min" } },
  entries = {
    { build = "userspace-root/font-min.pxb", at = "font-min.pxb" },
  },
}
