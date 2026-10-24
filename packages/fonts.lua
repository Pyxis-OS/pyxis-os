-- Cached, unchanged fonts with full family notices and provenance.
-- Run make font-assets before packing this recipe.
return {
  id = "fonts",
  version = "2026.10.24-blurgtext",
  kind = "data",
  depends = {},
  entries = {
    { tree = "../build/ports/fonts/stage/share/fonts", at = "" },
  },
}
