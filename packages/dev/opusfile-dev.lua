return {
  id = "opusfile-dev",
  variant = "0.12",
  kind = "dev",
  depends = { { id = "libogg-dev" }, { id = "opus-dev" } },
  entries = { { build = "ports/opusfile/stage/dev", at = "" } },
}
