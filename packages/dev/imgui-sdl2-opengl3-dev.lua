return {
  id = "imgui-sdl2-opengl3-dev",
  variant = "1.91.9b-docking",
  kind = "dev",
  depends = { { id = "imgui-dev" }, { id = "sdl2-osmesa-dev" } },
  entries = { { build = "ports/imgui/stage/sdl2-gl-dev", at = "" } },
}
