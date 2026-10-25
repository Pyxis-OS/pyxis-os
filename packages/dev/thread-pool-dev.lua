return {
  id = "thread-pool-dev",
  variant = "4.1.0",
  kind = "dev",
  depends = { { id = "libcxx-dev" } },
  entries = { { build = "ports/thread-pool/stage/dev", at = "" } },
}
