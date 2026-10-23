-- The selected static libpng profile for the installed SDK view.
return {
  id = "libpng-dev",
  variant = "1.6.59",
  kind = "dev",
  depends = { { id = "pyxis-dev" }, { id = "zlib-dev" } },
  entries = {
    { build = "ports/libpng/stage/dev", at = "" },
  },
}
