-- Optional first-party space editor, built on the pinned tview and tcell.
return {
  id = "spaces",
  variant = "tview-0.42.0",
  kind = "program",
  depends = {},
  commands = { spaces = { path = "spaces.pxb", command = "spaces" } },
  entries = {
    { build = "spaces/stage/bin/spaces.pxb", at = "spaces.pxb" },
  },
}
