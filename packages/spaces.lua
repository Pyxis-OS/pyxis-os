-- Optional first-party space editor, built on the pinned tview and tcell. It has
-- no upstream release, so its variant is its own version, like base.
return {
  id = "spaces",
  variant = "1",
  kind = "program",
  depends = {},
  commands = { spaces = { path = "spaces.pxb", command = "spaces" } },
  entries = {
    { build = "spaces/stage/bin/spaces.pxb", at = "spaces.pxb" },
  },
}
