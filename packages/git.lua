-- Optional first-party Git CLI, built on the pinned go-git library.
return {
  id = "git",
  variant = "go-git-5.19.3",
  kind = "program",
  depends = {},
  commands = { git = { path = "git.pxb", command = "git" } },
  entries = {
    { build = "git/stage/bin/git.pxb", at = "git.pxb" },
  },
}
