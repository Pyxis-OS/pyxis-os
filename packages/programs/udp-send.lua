-- udp-send from the Pyxis userland.
return {
  id = "udp-send",
  variant = "1",
  kind = "program",
  depends = {},
  commands = { ["udp-send"] = { path = "udp-send.pxe" } },
  entries = {
    { build = "userspace-root/udp-send.pxe", at = "udp-send.pxe" },
  },
}
