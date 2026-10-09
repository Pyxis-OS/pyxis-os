-- Ordinary manual audio qualification: nine shell spaces, no autoplay.
return {
  volumes = { home = { kind = "ram" } },
  spaces = {
    { name = "development", title = "Development", init = "boot://init",
      network = true, launch = true, roots = { home = "read-write" } },
    { name = "remote", title = "Remote", init = "boot://init-remote",
      roots = { home = "read-write" }, start = "tmp" },
    { name = "audio1", title = "Audio 1", init = "boot://init-readonly",
      roots = { home = "read-only" } },
    { name = "audio2", title = "Audio 2", init = "boot://init-readonly",
      roots = { home = "read-only" } },
    { name = "audio3", title = "Audio 3", init = "boot://init-readonly",
      roots = { home = "read-only" } },
    { name = "audio4", title = "Audio 4", init = "boot://init-readonly",
      roots = { home = "read-only" } },
    { name = "audio5", title = "Audio 5", init = "boot://init-readonly",
      roots = { home = "read-only" } },
    { name = "audio6", title = "Audio 6", init = "boot://init-readonly",
      roots = { home = "read-only" } },
    { name = "audio7", title = "Audio 7", init = "boot://init-readonly",
      roots = { home = "read-only" } },
  },
}
