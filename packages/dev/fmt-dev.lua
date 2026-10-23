-- The selected static fmt profile for the installed SDK view.
return {
  id = "fmt-dev",
  variant = "12.2.0",
  kind = "dev",
  depends = { { id = "libcxx-dev" } },
  entries = {
    { build = "ports/fmt/stage/dev", at = "" },
  },
}
