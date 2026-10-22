-- printf from the busybox port.
return {
  id = "printf",
  variant = "1",
  kind = "program",
  depends = {},
  commands = { printf = { path = "printf.pxe" } },
  entries = {
    { build = "ports/busybox/stage/bin/printf.pxe", at = "printf.pxe" },
    { build = "ports/busybox/stage/share/licenses/busybox", at = "licenses/busybox" },
  },
}
