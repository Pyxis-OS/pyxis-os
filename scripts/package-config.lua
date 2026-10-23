-- Writes the archive's config/packages.lua: one repository named public at
-- PACKAGE_REPOSITORY, or none when it is empty (docs/wip/pkg-upgrade.md,
-- decisions 65, 68 and 72). Unless PACKAGE_REPOSITORY_SIGNED is 1 the entry
-- says signed = false, as only development images do. pkg validates the URL
-- when it reads the file.
local url = os.getenv("PACKAGE_REPOSITORY") or ""
local signed = os.getenv("PACKAGE_REPOSITORY_SIGNED") or "0"
assert(signed == "0" or signed == "1", "PACKAGE_REPOSITORY_SIGNED must be 0 or 1")
local output = assert(arg[1], "usage: package-config.lua OUTPUT")
local text
if url == "" then
  text = "return {\n  repositories = {},\n}\n"
else
  assert(url:match("^https?://[^%s\"'\\]+$"), "PACKAGE_REPOSITORY must be an http:// or https:// URL")
  if url:sub(-1) ~= "/" then
    url = url .. "/"
  end
  text = string.format("return {\n  repositories = {\n    { name = \"public\", url = %q%s },\n  },\n}\n",
    url, signed == "1" and "" or ", signed = false")
end
local file = assert(io.open(output, "wb"))
file:write(text)
file:close()
