return function(inputs)
  local entries = {
    { tree = inputs.userspace, at = "" },
    { tree = inputs.ports, at = "" },
    { tree = inputs.sdk .. "/sysroot/usr/include", at = "sdk/usr/include" },
    { tree = inputs.sdk .. "/share/licenses", at = "sdk/share/licenses" },
    { tree = inputs.sdk .. "/share/toolchain", at = "sdk/share/toolchain" },
    { file = inputs.provenance, at = "sdk/manifest.txt" },
    { file = "third_party/limine/BOOTX64.EFI", at = "share/installer/BOOTX64.EFI" },
    { file = "boot/limine/limine.conf", at = "share/installer/limine.conf.template" },
    { file = "third_party/limine/LICENSE", at = "share/licenses/limine/LICENSE" },
  }
  for _, library in ipairs({ "crt0.o", "libc.a", "libterm.a", "libpyxis.a", "libnpfs-format.a", "libgcc.a" }) do
    entries[#entries + 1] = {
      file = inputs.sdk .. "/sysroot/usr/lib/" .. library,
      at = "sdk/usr/lib/" .. library,
    }
  end
  if inputs.init ~= "" then
    entries[#entries + 1] = { file = inputs.init, at = "init", replace = true }
  end
  if inputs.network_config ~= "" then
    entries[#entries + 1] = { file = inputs.network_config, at = "config/network.lua", replace = true }
  end

  local wad = inputs.wad
  if wad == "" then
    wad = "third_party/doom-shareware/doom1.wad"
    for _, name in ipairs({ "LICENSE", "UPSTREAM.md" }) do
      entries[#entries + 1] = { file = "third_party/doom-shareware/" .. name,
        at = "share/licenses/doom-shareware/" .. name }
    end
  end
  entries[#entries + 1] = { file = wad, at = "share/doom/DOOM.WAD" }
  if inputs.demos ~= "" then
    for _, name in ipairs({ "e1m1sec.lmp", "e1m2sec.lmp" }) do
      entries[#entries + 1] = { file = inputs.demos .. "/" .. name,
        at = "share/doom/" .. name }
    end
  end

  -- QUAKE_DATA paks arrive resolved, under the lowercase names Quake expects.
  local paks = { inputs.quake_pak0, inputs.quake_pak1 }
  if paks[1] == "" then
    paks = { "third_party/quake-shareware/pak0.pak" }
    for _, name in ipairs({ "SLICNSE.TXT", "LICINFO.TXT", "UPSTREAM.md" }) do
      entries[#entries + 1] = { file = "third_party/quake-shareware/" .. name,
        at = "share/licenses/quake-shareware/" .. name }
    end
  end
  for index, pak in ipairs(paks) do
    if pak ~= "" then
      entries[#entries + 1] = { file = pak, at = "share/quake/id1/pak" .. (index - 1) .. ".pak" }
    end
  end
  return entries
end
