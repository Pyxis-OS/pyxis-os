-- udp-echo, a development tool from the Pyxis userland.
return {
  id = "udp-echo",
  variant = "1",
  kind = "program",
  depends = {},
  commands = { ["udp-echo"] = { path = "udp-echo.pxe" } },
  entries = {
    { build = "userspace-root/udp-echo.pxe", at = "udp-echo.pxe" },
  },
}
