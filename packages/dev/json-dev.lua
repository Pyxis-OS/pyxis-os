return {
  id = "json-dev",
  variant = "3.12.0",
  kind = "dev",
  depends = { { id = "libcxx-dev" } },
  entries = { { build = "ports/json/stage/dev", at = "" } },
}
