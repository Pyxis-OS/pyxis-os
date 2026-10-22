-- pkg-config is the conventional name for the same pkgconf executable.
return {
  id = "pkgconf",
  variant = "1",
  kind = "program",
  depends = {},
  commands = {
    pkgconf = { path = "pkgconf.pxe" },
    ["pkg-config"] = { path = "pkgconf.pxe" },
  },
  entries = {
    { build = "ports/pkgconf/stage/bin/pkgconf.pxe", at = "pkgconf.pxe" },
    { build = "ports/pkgconf/stage/share/licenses/pkgconf", at = "licenses/pkgconf" },
  },
}
