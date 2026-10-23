-- Writes the archive's config/packages.lua (docs/wip/pkg-upgrade.md, decisions
-- 65, 68, 72 and 74): a repository named public at PACKAGE_REPOSITORY and one
-- named releases at RELEASE_REPOSITORY, each left out when its URL is empty.
-- Unless PACKAGE_REPOSITORY_SIGNED is 1 the public entry says signed = false,
-- as only development images do; releases is always signed. pkg validates the
-- URLs when it reads the file.
local signed = os.getenv("PACKAGE_REPOSITORY_SIGNED") or "0"
assert(signed == "0" or signed == "1", "PACKAGE_REPOSITORY_SIGNED must be 0 or 1")
local output = assert(arg[1], "usage: package-config.lua OUTPUT")

local function base_url(variable)
  local url = os.getenv(variable) or ""
  if url == "" then
    return nil
  end
  assert(url:match("^https?://[^%s\"'\\]+$"), variable .. " must be an http:// or https:// URL")
  if url:sub(-1) ~= "/" then
    url = url .. "/"
  end
  return url
end

local entries = {}
local public = base_url("PACKAGE_REPOSITORY")
if public then
  entries[#entries + 1] = string.format("    { name = \"public\", url = %q%s },\n",
    public, signed == "1" and "" or ", signed = false")
end
local releases = base_url("RELEASE_REPOSITORY")
if releases then
  entries[#entries + 1] = string.format("    { name = \"releases\", url = %q },\n", releases)
end
local text
if #entries == 0 then
  text = "return {\n  repositories = {},\n}\n"
else
  text = "return {\n  repositories = {\n" .. table.concat(entries) .. "  },\n}\n"
end
local file = assert(io.open(output, "wb"))
file:write(text)
file:close()
