set pagination off
set confirm off
target remote 127.0.0.1:12411
hbreak private_memory_allocate
set $n = 0
commands 1
  silent
  printf "hit cpu=%d process=%p root=0x%lx cr3=0x%lx\n", ((struct cpu_local *)$gs_base)->index, process, process->address_space->arch.root, $cr3
  set $n = $n + 1
  if $n < 1500
    continue
  end
end
continue
delete
detach
quit
