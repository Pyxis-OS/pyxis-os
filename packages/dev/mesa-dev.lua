return {
  id = "mesa-dev",
  variant = "26.2.5",
  kind = "dev",
  depends = { { id = "libcxx-dev" } },
  entries = {
    { build = "ports/mesa/stage/dev", at = "" },
  },
}
