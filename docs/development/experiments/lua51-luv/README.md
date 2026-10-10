# Lua 5.1 with LPeg and luv qualification

QEMU checks for task 5 of the [Neovim milestone](../../../wip/neovim-libuv.md):
the [`lua51` recipe](../../../../ports/lua51/README.md) and its
`boot://share/lua51/lua5.1.pxb` bundle, built from this change on main
`d01fe599` and rebased onto `441fcd4a` (#674), with userland's six added
math functions. The timer and child checks were repeated on the rebased build
with the same results.

## Configuration

- **QEMU:** 10.2.2 with the local AHCI fix, nested KVM on the development VM,
  q35, 8 GiB, standard VGA at 1280x800; 4 CPUs unless noted.
- **Tools:** scratch scripts and a scratch child program, never committed, in a
  read-write `host://` share. `child lines N STATUS` prints N lines 200 ms apart
  and exits with STATUS; `child fault` writes through a null pointer.
- **Sources:** all four archives came from the owner mirrors and matched the
  pinned SHA-256s before patching (the ports driver checks them on every build).

## Timers with child processes

A script started a 100 ms repeating timer (stopped after 8 ticks) and a 250 ms
one-shot, and spawned three children: A (`lines 4 7`) and B (`fault`) with
stdout on a luv pipe, and C (`lines 2 0`) inheriting the console. Run on the
console in a Development tab, with 4 CPUs:

- the ticks arrived at 99–100 ms intervals and the one-shot at 249 ms, while A's
  lines arrived 200 ms apart through its pipe and C's lines appeared directly;
- B's callback reported `code=-1 signal=0 reason=faulted` at 32 ms, C's
  `code=0 reason=exited`, and A's `code=7 reason=exited` at 809 ms;
- `uv.spawn` returned a handle and `nil` for each PID; the loop ended at 809 ms.

The same script with stdout redirected to a `host://` file gave the same
timings; C's spawn then returned `nil, "ENOSYS: …"`, because libuv refuses to
let a child inherit a FILE descriptor. With 1 CPU the ticks kept 100 ms
intervals (99–808 ms), B faulted at 46 ms and A exited with 7 at 811 ms.

Unsupported calls each returned `nil, "ENOSYS: function not implemented",
"ENOSYS"`: `uv.new_tcp`, `uv.getaddrinfo`, `uv.os_getpid`, `uv.queue_work`,
`uv.new_thread`, `uv.new_fs_event` and `uv.loadavg`.

## Working directory and environment

After libc gained a shared working path and mutable environment (#674), a
script started in `home://` checked them through Lua and luv:

- `os.getenv("TERM")` and `uv.os_getenv("TERM")` both gave `pyxis`; a variable
  set with `uv.os_setenv` appeared in both and in `uv.os_environ()` (6
  entries), and `uv.os_unsetenv` removed it from `os.getenv`.
- `uv.chdir("host://")` changed `uv.cwd()`, `require` found a module there and
  a relative `io.open` succeeded; a missing directory returned
  `nil, "ENOENT: …", "ENOENT"`.
- A spawned Lua 5.5 child inherited a variable and the working directory. With
  `env = {"ONLY=this"}` and `cwd = "home://"` it saw only `ONLY` and could not
  open the relative file.

## Standard libraries and LPeg

- **Math:** `sin`, `asin`, `acos`, `exp`, `sinh`, `cosh`, `tanh`, `floor` and
  `fmod` gave the expected values (for example `math.exp(1)` 2.718281828459).
- **Strings and tables:** formatting, `gsub`, bytewise comparison and
  `table.sort` behaved as upstream.
- **os and io:** `os.time()` and `os.date` worked; `os.time{…}` raised
  "calendar tables are not supported"; `os.execute`, `os.clock`,
  `os.setlocale`, `io.popen` and `package.loadlib` were `nil`.
  `os.tmpname` returned `tmp://lua_8aPTQY`; writing it, reading back with
  `read("*n")` and removing it worked, as did `io.tmpfile`.
- **Other:** `debug.traceback`, coroutines, `package.loaders` (preload and Lua
  files), `require "lpeg"` (`LPeg 1.1.0`, a captured list `10+20+30`) and
  `require` of a module from the working directory.
- **Command line:** script arguments reached `arg` and `...`, `-v` printed
  `Lua 5.1.5`, `-e` ran a chunk, and no arguments printed usage and exited 1.

## Comparison with Lua 5.5

For information only, not a baseline: a Lua 5.1 script ran the same CPU loop
(30 million additions, then building and joining 200,000 strings) in-process
and through a spawned `bin://lua.pxe` (Lua 5.5), three rounds each, 4 CPUs:

| Round | Lua 5.1 in-process | Lua 5.5 child, including launch |
| --- | --- | --- |
| 1 | 345 ms | 337 ms |
| 2 | 243 ms | 290 ms |
| 3 | 265 ms | 281 ms |

## Native steps for the owner

On the ThinkPad with a PXE build of this branch, from a Development tab:

1. `boot://share/lua51/lua5.1.pxb -v` prints `Lua 5.1.5`.
2. Save this script as `home://timers.lua` (it runs Lua 5.5 from the image as
   the child) and run `boot://share/lua51/lua5.1.pxb home://timers.lua`:

   ```lua
   local uv = require("luv")
   local start = uv.hrtime()
   local function ms() return math.floor((uv.hrtime() - start) / 1e6) end
   local ticks = 0
   local timer = uv.new_timer()
   timer:start(100, 100, function()
     ticks = ticks + 1
     print(("tick %d at %d ms"):format(ticks, ms()))
     if ticks == 5 then timer:close() end
   end)
   local out = uv.new_pipe(false)
   local child
   child = uv.spawn("bin://lua.pxe", {
     args = {"-e", "for i = 1, 3 do print('child line ' .. i) end os.exit(7)"},
     stdio = {nil, out, 2},
   }, function(code, signal, reason)
     print(("child exit at %d ms: code=%d signal=%d reason=%s"):format(ms(), code, signal, reason))
     child:close()
   end)
   out:read_start(function(err, data)
     if data then io.write(data) else out:close() end
   end)
   uv.run()
   print("done")
   ```

   The child's three lines and `child exit … code=7 signal=0 reason=exited`
   come first, then five ticks about 100 ms apart and `done`. In QEMU the exit
   came at 8 ms and the ticks at 99–502 ms.

## Limits

- QEMU only; no native run yet.
- The checks are interactive runs with scratch scripts, not a test suite.
- Lua 5.1's remaining standard-library gaps and luv's unsupported families are
  listed in [technical debt](../../../technical-debt.md#lua-51-and-luv-limits).
