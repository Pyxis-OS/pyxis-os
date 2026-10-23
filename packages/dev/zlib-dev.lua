-- The selected static zlib profile for the installed SDK view.
return {
  id = "zlib-dev",
  variant = "1.3.2",
  kind = "dev",
  depends = { { id = "pyxis-dev" } },
  entries = {
    { build = "ports/zlib/stage/dev", at = "" },
  },
}
