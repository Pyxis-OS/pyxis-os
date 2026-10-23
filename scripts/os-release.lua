-- Usage: lua scripts/os-release.lua boot/os-release.lua REVISION [RELEASE [FLOOR]]
-- Prints boot://os-release: the release tag at HEAD, if any, the build revision,
-- the source's release ABI list and, for a Git build, signed_floor, the build
-- commit's UTC time.
local source, revision, release, floor = arg[1], arg[2] or "", arg[3] or "", arg[4] or ""

local function fail(reason)
  io.stderr:write("os-release: ", reason, "\n")
  os.exit(1)
end

local function abi_name(name)
  return type(name) == "string" and #name <= 64 and name:match("^[0-9a-z][0-9a-z.-]*$") ~= nil
end

local ok, table_or_error = pcall(dofile, source)
if not ok then
  fail(table_or_error)
end
local list = type(table_or_error) == "table" and table_or_error.abi
if type(list) ~= "table" then
  fail(source .. " has no abi list")
end
for key in pairs(table_or_error) do
  if key ~= "abi" then
    fail(source .. " names " .. tostring(key) .. "; only abi is maintained there")
  end
end

local entries = 0
for _ in pairs(list) do
  entries = entries + 1
end
if entries ~= #list then
  fail(source .. ": abi must be a plain list")
end
local names = {}
for index, name in ipairs(list) do
  if not abi_name(name) then
    fail(source .. ": abi entry " .. index .. " is not an ABI name")
  end
  -- A letter release such as 0.0.8a provides its base release's ABI name.
  if name:match("^%d+%.%d+%.%d+%l$") then
    fail(source .. ": " .. name .. " is a letter release; list its base release instead")
  end
  names[#names + 1] = string.format("%q", name)
end
if release ~= "" and not abi_name(release) then
  fail("release tag " .. release .. " is not an ABI name")
end
if floor ~= "" and not floor:match("^%d%d%d%d%-%d%d%-%d%dT%d%d:%d%d:%d%dZ$") then
  fail("signed_floor " .. floor .. " is not a UTC time")
end

io.write("return {\n")
if release ~= "" then
  io.write(string.format("  release = %q,\n", release))
end
io.write(string.format("  revision = %q,\n", revision))
if #names == 0 then
  io.write("  abi = {},\n")
else
  io.write("  abi = { ", table.concat(names, ", "), " },\n")
end
if floor ~= "" then
  io.write(string.format("  signed_floor = %q,\n", floor))
end
io.write("}\n")
