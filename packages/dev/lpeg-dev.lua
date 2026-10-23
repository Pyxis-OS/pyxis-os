-- Separate SDK ownership; retain the shared Lua-family notices/provenance.
return {
  id = "lpeg-dev",
  variant = "1.1.0",
  kind = "dev",
  depends = { { id = "pyxis-dev" }, { id = "lua5.1-dev" } },
  entries = {
    { build = "ports/lua51/stage/dev/lib/liblpeg.a", at = "lib/liblpeg.a" },
    { build = "ports/lua51/stage/dev/lib/pkgconfig/lpeg.pc", at = "lib/pkgconfig/lpeg.pc" },
    { build = "ports/lua51/stage/dev/share", at = "share" },
  },
}
