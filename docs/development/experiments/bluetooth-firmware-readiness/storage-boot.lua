return {
  spaces = {
    { name = "pyxis", title = "Bluetooth firmware", init = "boot://init",
      network = true, roots = { system = "read-only" }, start = "tmp" },
  },
}
