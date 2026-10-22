-- Compiled development libraries are tied to the selected system ABI.
return {
  id = "raylib-dev",
  version = "6.2",
  kind = "toolchain",
  depends = {},
  entries = {
    { build = "ports/raylib/stage/dev", at = "raylib" },
    { build = "ports/sdl2/stage/dev", at = "sdl2" },
  },
}
