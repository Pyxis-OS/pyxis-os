#!/usr/bin/env python3
"""SMP task 8 native check, driven from the desktop over the remote terminal.

usage: smp8-check.py CLIENT LABEL [HOST [PORT]]          run the measured batches
       smp8-check.py CLIENT --load SECONDS [HOST [PORT]] keep every CPU busy

CLIENT is the path to build/tools/pyxis-remote. Each batch opens one remote
session, starts N background clients there, and waits for all N results. The
measured run prints a summary and also writes it to smp8-LABEL.txt.
"""
import base64, json, re, subprocess, sys, time

HEAP = "allocbench heap --rounds 262144"
PAGES = "allocbench pages --rounds 2048"
BATCHES = [("heap", HEAP, n) for n in (1, 4, 8, 11)] + [("pages", PAGES, n) for n in (1, 2, 4, 8)]
LOAD_HEAP = "allocbench heap --rounds 1000000"
LOAD_PAGES = "allocbench pages --rounds 20000"


class Session:
    def __init__(self, client, host, port):
        command = [client, "--machine", "--no-shell-echo", "--columns", "120", "--rows", "40",
                   host, str(port)]
        self.process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        first = self.process.stdout.readline()
        if not first or json.loads(first).get("type") != "ready":
            sys.exit("remote session did not start; is the network up and the server ready?")
        self.text = ""

    def event(self):
        line = self.process.stdout.readline()
        if not line:
            sys.exit("remote session ended unexpectedly")
        event = json.loads(line)
        if event["type"] == "output":
            self.text += base64.b64decode(event["base64"]).decode(errors="replace")
        return event

    def launch(self, command):
        self.process.stdin.write((command + " &\n").encode())
        self.process.stdin.flush()
        while True:
            event = self.event()
            if event["type"] == "command_complete":
                if event.get("kind") != "launched":
                    sys.exit(f"could not start {command!r}: {event}")
                return

    def wait_for(self, count, timeout):
        deadline = time.monotonic() + timeout
        while self.text.count("Elapsed") < count:
            if time.monotonic() > deadline:
                sys.exit(f"timed out waiting for {count} results")
            self.event()
        failures = [int(x) for x in re.findall(r"(\d+) failures", self.text)]
        return [float(x) for x in re.findall(r"Elapsed ([\d.]+) ms", self.text)], failures

    def close(self):
        self.process.stdin.write(b"exit\n")
        self.process.stdin.close()
        self.process.stdout.read()
        self.process.wait()


def batch(client, host, port, commands, timeout=300):
    session = Session(client, host, port)
    start = time.monotonic()
    for command in commands:
        session.launch(command)
    elapsed, failures = session.wait_for(len(commands), timeout)
    wall = time.monotonic() - start
    session.close()
    return wall, elapsed, failures


def measured(client, label, host, port):
    lines = []
    batch(client, host, port, [PAGES])  # warm-up, not reported
    for name, command, count in BATCHES:
        wall, elapsed, failures = batch(client, host, port, [command] * count)
        line = (f"{name} x{count}: wall {wall:.3f} s; each "
                + ", ".join(f"{value / 1000:.3f}" for value in sorted(elapsed))
                + f" s; failures {sum(failures)}")
        print(line, flush=True)
        lines.append(line)
    with open(f"smp8-{label}.txt", "w") as output:
        output.write("\n".join(lines) + "\n")
    print(f"written to smp8-{label}.txt")


def load(client, seconds, host, port):
    end = time.monotonic() + seconds
    rounds = 0
    while time.monotonic() < end:
        rounds += 1
        wall, _, failures = batch(client, host, port, [LOAD_HEAP] * 11 + [LOAD_PAGES] * 4)
        print(f"load round {rounds}: 15 clients in {wall:.1f} s, failures {sum(failures)}",
              flush=True)


def main(argv):
    if len(argv) < 3:
        sys.exit(__doc__)
    client = argv[1]
    if argv[2] == "--load":
        seconds = float(argv[3])
        rest = argv[4:]
    else:
        label = argv[2]
        rest = argv[3:]
    host = rest[0] if rest else "192.168.0.50"
    port = int(rest[1]) if len(rest) > 1 else 2323
    if argv[2] == "--load":
        load(client, seconds, host, port)
    else:
        measured(client, label, host, port)


main(sys.argv)
