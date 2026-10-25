return {
  id = "opus-dev",
  variant = "1.6.1",
  kind = "dev",
  depends = { { id = "pyxis-dev" } },
  entries = { { build = "ports/opus/stage/dev", at = "" } },
}
