return {
  id = "monocypher-dev",
  variant = "4.0.2",
  kind = "dev",
  depends = { { id = "pyxis-dev" } },
  entries = { { build = "ports/monocypher/stage/dev", at = "" } },
}
