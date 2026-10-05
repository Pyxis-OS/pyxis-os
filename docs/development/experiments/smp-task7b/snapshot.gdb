set pagination off
target remote 127.0.0.1:12411
python
import gdb
def desc(t):
    if "USER" in str(t["kind"]):
        sp = t["process"]["space"]
        return "user(" + sp["title"].string() + ")"
    return "kernel"
line=[]
for cpu in range(int(gdb.parse_and_eval("cpu_count"))):
    s = gdb.parse_and_eval(f"schedulers[{cpu}]")
    cur = s["current_task"]
    items = ["run " + (desc(cur) if int(cur) else "idle")]
    q = s["ready_head"]
    while int(q):
        items.append("q " + desc(q)); q = q["next"]
    line.append(f"CPU{cpu}: " + ", ".join(items))
print("SNAP " + " | ".join(line))
end
detach
quit
