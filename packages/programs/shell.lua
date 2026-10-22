-- shell from the Pyxis userland.
return {
  id = "shell",
  variant = "1",
  kind = "program",
  depends = {},
  commands = { shell = { path = "shell.pxb", command = "shell" } },
  entries = {
    { build = "userspace-root/shell.pxb", at = "shell.pxb" },
  },
}
