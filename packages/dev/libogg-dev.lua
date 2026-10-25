return {
  id = "libogg-dev",
  variant = "1.3.6",
  kind = "dev",
  depends = { { id = "pyxis-dev" } },
  entries = { { build = "ports/libogg/stage/dev", at = "" } },
}
