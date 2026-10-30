-- Usage: lua scripts/os-release.lua REVISION RELEASE VERSION UPGRADE_FROM FLOOR [ABI...]
-- Git authority and the shared line-preservation comparison live in
-- abi-contract.sh; this script only writes the generated literal record.
local revision, release, version, upgrade_from, floor = table.unpack(arg, 1, 5)

io.write("return {\n")
if release ~= "" then
  io.write(string.format("  release = %q,\n", release))
else
  io.write(string.format("  version = %q,\n", version))
end
io.write(string.format("  revision = %q,\n", revision))
local names = {}
for index = 6, #arg do
  names[#names + 1] = string.format("%q", arg[index])
end
if #names == 0 then
  io.write("  abi = {},\n")
else
  io.write("  abi = { ", table.concat(names, ", "), " },\n")
end
if upgrade_from ~= "" then
  io.write(string.format("  upgrade_from = %q,\n", upgrade_from))
end
io.write(string.format("  signed_floor = %q,\n", floor))
io.write("}\n")
