-- mux from the Pyxis userland.
return {
  id = "mux",
  variant = "1",
  kind = "program",
  depends = {},
  commands = { mux = { path = "mux.pxe" } },
  entries = {
    { build = "userspace-root/mux.pxe", at = "mux.pxe" },
  },
}
