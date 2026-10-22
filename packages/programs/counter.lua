-- counter, a development tool from the Pyxis userland.
return {
  id = "counter",
  variant = "1",
  kind = "program",
  depends = {},
  commands = { counter = { path = "counter.pxe" } },
  entries = {
    { build = "userspace-root/counter.pxe", at = "counter.pxe" },
  },
}
