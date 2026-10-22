-- screenshot from the Pyxis userland.
return {
  id = "screenshot",
  variant = "1",
  kind = "program",
  depends = {},
  commands = { screenshot = { path = "screenshot.pxe" } },
  entries = {
    { build = "userspace-root/screenshot.pxe", at = "screenshot.pxe" },
    { build = "ports/libpng/stage/share/licenses/libpng", at = "licenses/libpng" },
    { build = "ports/zlib/stage/share/licenses/zlib", at = "licenses/zlib" },
  },
}
