-- Trusted manifests select regular files and trees. This is assembly policy,
-- not a recipe executor or a package/dependency manager.
local function quote(value)
  assert(type(value) == "string" and not value:find("\0", 1, true), "invalid argument")
  return "'" .. value:gsub("'", "'\\''") .. "'"
end

local function command(args)
  local words = {}
  for i, value in ipairs(args) do words[i] = quote(value) end
  return table.concat(words, " ")
end

local function run(args)
  assert(os.execute(command(args)), "command failed: " .. command(args))
end

local function capture(args)
  local pipe = assert(io.popen(command(args), "r"))
  local result = pipe:read("a")
  assert(pipe:close(), "command failed: " .. command(args))
  return result
end

local function relative(path)
  assert(type(path) == "string" and not path:find("\0", 1, true), "invalid destination")
  assert(path:sub(1, 1) ~= "/", "destination must be relative: " .. path)
  for part in path:gmatch("[^/]+") do
    assert(part ~= "." and part ~= "..", "invalid destination: " .. path)
  end
  return path:gsub("/+", "/"):gsub("/$", "")
end

local function main()
  local manifest, destination = arg[1], arg[2]
  assert(manifest and destination and destination ~= "" and destination ~= "/" and
    destination ~= ".", "Usage: lua scripts/stage-tree.lua MANIFEST OUTPUT [NAME=PATH ...]")
  local inputs = {}
  for i = 3, #arg do
    local name, path = arg[i]:match("^([%w_]+)=(.*)$")
    assert(name and not inputs[name], "invalid/repeated input: " .. arg[i])
    inputs[name] = path
  end
  local entries = assert(loadfile(manifest))()(inputs)
  local staging = destination .. ".tmp"
  run({ "rm", "-rf", "--", staging })
  run({ "mkdir", "-p", "--", staging })
  local occupied = { [""] = "directory" }

  local function directory(path)
    if occupied[path] then
      assert(occupied[path] == "directory", "file/directory collision: " .. path)
      return
    end
    directory(path:match("^(.*)/") or "")
    run({ "mkdir", "--", staging .. "/" .. path })
    occupied[path] = "directory"
  end

  local function file(source, path, replace)
    assert(path ~= "", "file destination cannot be empty")
    assert(not occupied[path] or (replace and occupied[path] == "file"),
      "duplicate destination: " .. path)
    run({ "test", "-f", source })
    run({ "test", "!", "-L", source })
    directory(path:match("^(.*)/") or "")
    run({ "install", "-m", "644", "--", source, staging .. "/" .. path })
    occupied[path] = "file"
  end

  for _, entry in ipairs(entries) do
    local at = relative(entry.at)
    assert((entry.file ~= nil) ~= (entry.tree ~= nil), "entry needs file or tree")
    if entry.file then
      file(entry.file, at, entry.replace)
    else
      assert(not entry.replace, "replace applies only to explicit files")
      run({ "test", "-d", entry.tree })
      run({ "test", "!", "-L", entry.tree })
      directory(at)
      local listing = capture({ "find", entry.tree, "-mindepth", "1", "-printf", "%y%P\\0" })
      -- entry.exclude lists subtrees, relative to the tree, left out of the archive.
      local function excluded(name)
        for _, prefix in ipairs(entry.exclude or {}) do
          if name == prefix or name:sub(1, #prefix + 1) == prefix .. "/" then
            return true
          end
        end
        return false
      end
      local members = {}
      for item in listing:gmatch("([^\0]+)\0") do
        if not excluded(item:sub(2)) then members[#members + 1] = item end
      end
      table.sort(members)
      for _, item in ipairs(members) do
        local kind, name = item:sub(1, 1), relative(item:sub(2))
        local path = at == "" and name or at .. "/" .. name
        if kind == "d" then
          directory(path)
        elseif kind == "f" then
          file(entry.tree .. "/" .. name, path, false)
        else
          error("unsupported file type: " .. entry.tree .. "/" .. name)
        end
      end
    end
  end
  -- newc records mtimes. Normalize them as well as modes and archive ownership.
  run({ "find", staging, "-type", "d", "-exec", "chmod", "755", "{}", "+" })
  run({ "find", staging, "-exec", "touch", "-h", "-d", "@0", "{}", "+" })
  if not os.execute(command({ "diff", "-qr", staging, destination }) .. " >/dev/null 2>&1") then
    run({ "rm", "-rf", "--", destination })
    run({ "mv", "--", staging, destination })
  else
    run({ "rm", "-rf", "--", staging })
  end
end

local ok, message = pcall(main)
if not ok then
  io.stderr:write("stage: " .. tostring(message) .. "\n")
  os.exit(1)
end
