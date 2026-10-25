return {
  id = "spdlog-dev",
  variant = "1.16.0",
  kind = "dev",
  depends = { { id = "libcxx-dev" } },
  entries = { { build = "ports/spdlog/stage/dev", at = "" } },
}
