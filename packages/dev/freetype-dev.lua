-- The plain static FreeType profile for the installed SDK view.
return {
  id = "freetype-dev",
  variant = "2.15.0",
  kind = "dev",
  depends = { { id = "pyxis-dev" } },
  entries = {
    { build = "ports/freetype/stage/dev", at = "" },
  },
}
