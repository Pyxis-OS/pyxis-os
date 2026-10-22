return {
  id = "raylib-shapes",
  variant = "6.2",
  kind = "program",
  depends = {},
  commands = {
    ["raylib-shapes"] = { path = "raylib-shapes.pxb", command = "raylib-shapes" },
  },
  entries = {
    { build = "ports/raylib/stage/bin/raylib-shapes.pxb", at = "raylib-shapes.pxb" },
    { build = "ports/raylib/stage/dev/share/licenses/raylib", at = "licenses/raylib" },
    { build = "ports/sdl2/stage/dev/share/licenses/sdl2", at = "licenses/sdl2" },
  },
}
