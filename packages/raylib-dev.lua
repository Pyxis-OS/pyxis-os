-- Compiled development libraries are tied to the selected system ABI.
return {
  id = "raylib-dev",
  variant = "6.2",
  kind = "dev",
  depends = { { id = "sdl2-dev" } },
  entries = {
    { build = "ports/raylib/stage/dev", at = "" },
  },
}
