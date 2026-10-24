return {
  id = "imgui-dev",
  variant = "1.91.9b-docking",
  kind = "dev",
  depends = { { id = "libcxx-dev" } },
  entries = { { build = "ports/imgui/stage/dev", at = "" } },
}
