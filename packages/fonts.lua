-- Cached, unchanged fonts with full family notices and provenance.
-- Run make font-assets before packing this recipe.
return {
  id = "fonts",
  version = "nerd3.5.1-plexsans1.1.0-plexserif2.0.0-noto2.015-notomono2.014",
  kind = "data",
  depends = {},
  entries = {
    { tree = "../build/ports/fonts/stage/share/fonts", at = "" },
  },
}
