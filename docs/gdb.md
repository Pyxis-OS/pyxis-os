# Calling Caelum functions from GDB

GDB can invoke functions already linked into the kernel, using their debug
symbols and C types. These calls execute in QEMU: allocations, memory writes,
and other side effects are real. The GDB manual calls this
[calling program functions](https://sourceware.org/gdb/current/onlinedocs/gdb.html/Calling.html).

## Connect and stop after initialization

Run `make debug` in one terminal, then `gdb -q build/caelum.elf` in another.
Use the ELF from the same build as the running image. In GDB:

```gdb
set pagination off
target remote localhost:1234
thbreak vm_get_stats
continue
```

`make debug` starts QEMU paused with its debugger listening on localhost:1234.
See [QEMU's GDB guide](https://www.qemu.org/docs/master/system/gdb.html).
`thbreak` sets a temporary hardware breakpoint, removed when hit. In the current
boot flow, the first `vm_get_stats()` call happens after PMM, paging, VM, and heap
initialization, so their functions are ready to use.

Keep interrupts disabled. Do not call allocators while stopped inside an
allocator mutation or a fault handler; their normal invariants still apply.
After scheduling starts, use a breakpoint in scheduler code with the kernel
space active and IF=0 for these calls. An interrupt handler or a stopped user
context is not a suitable place to invoke allocator or task-creation functions.

## Call functions and keep results

```gdb
p pmm_get_stats()
p heap_get_stats()
set $buffer = kmalloc(64)
p/x $buffer
```

`p` is short for `print`; it evaluates an expression and prints the result.
`call` also evaluates an expression, but omits a void result, making it useful
for `call kfree($buffer)`. Both forms execute any function calls they contain.

`$buffer` is a [GDB convenience variable](https://sourceware.org/gdb/current/onlinedocs/gdb.html/Convenience-Vars.html),
held by the debugger. Here it contains a pointer to actual kernel memory.
If the allocation returned a nonzero pointer, inspect and release it:

```gdb
x/4gx $buffer
call kfree($buffer)
```

[`x/4gx`](https://sourceware.org/gdb/current/onlinedocs/gdb.html/Memory.html)
examines four eight-byte words in hexadecimal. Ordinary GDB memory reads through
QEMU use the current virtual mappings; a physical frame address or an address
belonging only to an inactive space is not directly readable this way.

For an output parameter, allocate storage in the kernel. A convenience variable
itself has no kernel address you can pass as `&$space`. Run these commands one at
a time, proceeding after the allocation succeeds and each operation returns
`MM_OK`:

```gdb
set $slot = (struct vm_space **)kmalloc(sizeof(struct vm_space *))
p/x $slot
p vm_space_create($slot)
set $space = *$slot
p vm_alloc_at($space, 0x400000, PAGE_SIZE, PAGE_USER | PAGE_WRITE)
p vm_get_stats($space)
p *$space
p vm_space_destroy($space)
call kfree($slot)
```

The new space remains inactive. Destruction releases its user page, private
tables, and metadata; freeing `$slot` releases the separate output storage.

## Stepping into calls and the CR3 caveat

You can set `hbreak function_name` before calling that function, inspect its
arguments with `info args`, inspect instructions with `disassemble`, and advance
one instruction with `si`. If a breakpoint interrupts a debugger-initiated call,
`bt` shows a `<function called from gdb>` frame. `continue` lets the call finish.

GDB restores saved execution context after an injected call; it does not undo
memory changes. On the GDB/QEMU setup used here, that restoration included CR3.
Consequently, calling `vm_space_activate()` changed the kernel's `active_space`
pointer, but GDB restored the previous CR3 when the call returned.

To observe the switch itself, stop immediately after the `mov` to CR3 inside
`arch_space_activate()` and use `p/x $cr3`. After a successful completed manual
activation, reconcile CR3 with the kernel's bookkeeping before further calls,
continuing, or destroying a space:

```gdb
set $cr3 = (unsigned long)('arch/x86_64/paging.c'::active_space->root)
p/x $cr3
```

The filename selects the paging module's static variable. The explicit cast
avoids the register-assignment type error observed with a plain integer literal.
Apply the same reconciliation if you manually activate the kernel space again.
The allocation example above does not switch spaces and needs no such adjustment.

Use `continue` to resume boot, or `detach` followed by `quit` to leave GDB.
Exit QEMU with Ctrl-a x in its terminal. Restart QEMU for fresh kernel state.
