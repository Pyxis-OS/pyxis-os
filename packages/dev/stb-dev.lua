return {
  id = "stb-dev",
  variant = "2.29",
  kind = "dev",
  depends = { { id = "pyxis-dev" } },
  entries = { { build = "ports/stb/stage/dev", at = "" } },
}
