-- tar from the busybox port.
return {
  id = "tar",
  variant = "1",
  kind = "program",
  depends = {},
  commands = { tar = { path = "tar.pxe" } },
  entries = {
    { build = "ports/busybox/stage/bin/tar.pxe", at = "tar.pxe" },
    { build = "ports/busybox/stage/share/licenses/busybox", at = "licenses/busybox" },
  },
}
