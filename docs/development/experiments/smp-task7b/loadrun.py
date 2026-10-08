#!/usr/bin/env python3
# loadrun.py LOG JOBS FOREGROUND: start JOBS copies of a compute command in the
# background of one remote shell, run FOREGROUND, then wait for every job.
import base64, json, os, re, subprocess, sys, time
CLIENT = [os.path.expanduser("~/src/pyxis-smp/build/tools/pyxis-remote"), "--machine", "--no-shell-echo",
          "--columns", "160", "--rows", "50", "127.0.0.1", "23411"]
BG = "allocbench heap --rounds 1000000"
log, jobs, fg = sys.argv[1], int(sys.argv[2]), sys.argv[3]
p = subprocess.Popen(CLIENT, stdin=subprocess.PIPE, stdout=subprocess.PIPE)
assert json.loads(p.stdout.readline())["type"] == "ready"
out = []
def send(cmd):
    p.stdin.write((cmd + "\n").encode()); p.stdin.flush()
def until(pred):
    while True:
        ev = json.loads(p.stdout.readline())
        if ev["type"] == "output":
            out.append(base64.b64decode(ev["base64"]).decode(errors="replace"))
        if pred(ev):
            return ev
for i in range(jobs):
    send(BG + " &")
    until(lambda ev: ev["type"] == "command_complete")
t0 = time.monotonic()
send(fg)
ev = until(lambda ev: ev["type"] == "command_complete" and ev.get("kind") != "background")
fg_s = time.monotonic() - t0
while "".join(out).count("Elapsed") < jobs + (1 if fg.startswith("allocbench") else 0):
    until(lambda ev: ev["type"] == "output")
with open(log, "a") as f:
    f.write(f"### load {jobs}x [{BG}] + {fg}\n# foreground host {fg_s:.3f} s complete={json.dumps(ev)}\n{''.join(out)}\n")
print(f"{fg}: {fg_s:.3f}s", re.findall(r"= ([\d.]+) MiB/s", "".join(out)), re.findall(r"Elapsed ([\d.]+) ms", "".join(out)))
send("exit"); p.stdin.close(); p.stdout.read(); p.wait()
