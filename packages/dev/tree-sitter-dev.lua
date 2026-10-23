-- The selected static tree-sitter profile for the installed SDK view.
return {
  id = "tree-sitter-dev",
  variant = "0.26.13",
  kind = "dev",
  depends = { { id = "pyxis-dev" } },
  entries = {
    { build = "ports/tree-sitter/stage/dev", at = "" },
  },
}
