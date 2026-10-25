return {
  id = "libultraship-dev",
  variant = "0.git.da6daf1a50a5",
  kind = "dev",
  depends = {
    { id = "imgui-sdl2-opengl3-dev" }, { id = "libzip-dev" }, { id = "json-dev" },
    { id = "tinyxml2-dev" }, { id = "spdlog-dev" }, { id = "thread-pool-dev" },
    { id = "prism-dev" }, { id = "monocypher-dev" }, { id = "dr-libs-dev" },
    { id = "stb-dev" }, { id = "libvorbis-dev" }, { id = "opusfile-dev" },
  },
  entries = { { build = "ports/libultraship/stage/dev", at = "" } },
}
