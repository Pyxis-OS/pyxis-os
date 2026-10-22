-- ipcbench, a development tool from the Pyxis userland.
return {
  id = "ipcbench",
  variant = "1",
  kind = "program",
  depends = {},
  commands = { ipcbench = { path = "ipcbench.pxe" } },
  entries = {
    { build = "userspace-root/ipcbench.pxe", at = "ipcbench.pxe" },
  },
}
