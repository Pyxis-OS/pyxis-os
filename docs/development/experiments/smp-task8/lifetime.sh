#!/bin/bash
# t8life.sh TAG CPUS ISO ELF: lifetime scenarios on the default-config image
S=$(dirname $0); T=$1; N=$2; ISO=$3; ELF=$4; D=/dev/shm/pyxis-t5; cd $S || exit 1
rm -f $S/t8life-$T.txt
$S/t5boot.sh $T $N $ISO > /dev/null 2>&1 &
for i in $(seq 90); do grep -q "boot USB enumeration finished" $D/$T-serial.txt 2>/dev/null && break; sleep 1; done; sleep 6
rd() { printf 'set pagination off\ntarget remote 127.0.0.1:12411\nprintf "## %s\\n"\np '"'"'kernel/mm/pmm.c'"'"'::stats\np '"'"'kernel/mm/heap.c'"'"'::stats\ndetach\nquit\n' "$1" > $S/t8-rd.gdb; timeout 60 gdb -q -batch -x $S/t8-rd.gdb $ELF 2>/dev/null | grep -E "^## |^\\$" >> $S/t8life-$T.txt; }
python3 drive.py $S/t8life-$T-warm.txt seq "allocbench pages" "ls app://" > /dev/null
rd "idle before"
{
echo "### pipelines and rollback"
python3 drive.py $S/t8life-$T-cmd.txt seq "ls app:// | head -n 2" "cat app://share/iobench.bin | head -c 16 | cat" "ls app:// | app://missing.pxe" "ls app://missing-dir" 2>&1
echo "### Ctrl-C: blocked reader, running pipeline, compute job"
python3 ctrlc.py "head -n 1" 1.0 2>&1 | head -1
python3 ctrlc.py "cat app://share/iobench.bin | allocbench heap --rounds 1000000" 1.0 2>&1 | head -1
python3 ctrlc.py "allocbench heap --rounds 1000000" 0.5 2>&1 | head -1
echo "### session exit with background jobs"
python3 - <<'PY'
import json, os, subprocess, time
c = [os.path.expanduser("~/src/pyxis-smp/build/tools/pyxis-remote"), "--machine", "--no-shell-echo", "127.0.0.1", "23411"]
p = subprocess.Popen(c, stdin=subprocess.PIPE, stdout=subprocess.PIPE)
assert json.loads(p.stdout.readline())["type"] == "ready"
for i in range(4):
    p.stdin.write(b"allocbench heap --rounds 1000000 &\n"); p.stdin.flush()
    while json.loads(p.stdout.readline())["type"] != "command_complete": pass
time.sleep(0.5)
p.stdin.write(b"exit\n"); p.stdin.flush(); p.stdin.close()
last = None
for line in p.stdout:
    ev = json.loads(line)
    if ev["type"] not in ("output", "fresh_line"): last = ev
p.wait(); print("exit with 4 background jobs:", last)
PY
sleep 6
} >> $S/t8life-$T.txt 2>&1
python3 drive.py $S/t8life-$T-after.txt seq "allocbench pages" "ls app://" > /dev/null
rd "idle after"
sleep 20; python3 drive.py $S/t8life-$T-after2.txt seq "ls app://" > /dev/null; sleep 10
rd "idle after 30 s more"
sleep 100
rd "idle 130 s after the last session closed"
python3 mon.py $D/$T-mon.sock quit > /dev/null; sleep 1
echo "panics: $(grep -a -c -i panic $D/$T-serial.txt)" >> $S/t8life-$T.txt
cat $S/t8life-$T.txt
