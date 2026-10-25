-- Optional first-party dependent-add rate probe; no privileged hardware access.
return {
  id = "cpuclock",
  variant = "1",
  kind = "program",
  depends = {},
  commands = { cpuclock = { path = "cpuclock.pxb", command = "cpuclock" } },
  entries = {
    { build = "userspace-root/cpuclock.pxb", at = "cpuclock.pxb" },
  },
}
