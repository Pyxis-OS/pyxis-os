-- nvim from the neovim port.
return {
  id = "nvim",
  variant = "1",
  kind = "program",
  depends = {},
  commands = { nvim = { path = "nvim.pxb", command = "nvim" } },
  entries = {
    { build = "ports/neovim/stage/bin/nvim.pxb", at = "nvim.pxb" },
  },
}
