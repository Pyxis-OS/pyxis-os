-- First-party process list. Lists its own space implicitly; --all uses the
-- system process view its manifest requests (docs/userland/procs.md).
return {
  id = "procs",
  variant = "1",
  kind = "program",
  depends = {},
  commands = { procs = { path = "procs.pxb", command = "procs" } },
  entries = {
    { build = "userspace-root/procs.pxb", at = "procs.pxb" },
  },
}
