-- Native DualSense Edge input visualizer from Pyxis userland.
return {
  id = "gamepadtest",
  variant = "1",
  kind = "program",
  depends = { "fonts" },
  commands = { gamepadtest = { path = "gamepadtest.pxb", command = "gamepadtest" } },
  entries = {
    { build = "userspace-root/gamepadtest.pxb", at = "gamepadtest.pxb" },
  },
}
