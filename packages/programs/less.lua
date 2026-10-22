-- less from the busybox port.
return {
  id = "less",
  variant = "1",
  kind = "program",
  depends = {},
  commands = { less = { path = "less.pxe" } },
  entries = {
    { build = "ports/busybox/stage/bin/less.pxe", at = "less.pxe" },
    { build = "ports/busybox/stage/share/licenses/busybox", at = "licenses/busybox" },
  },
}
