#!/usr/bin/env python3
"""Summarize the finite native HID-I2C run from its saved guest log."""
import re
import sys
from pathlib import Path

if len(sys.argv) != 2:
    raise SystemExit("usage: summarize.py hid-i2c-task3.txt")
text = Path(sys.argv[1]).read_text(errors="replace")
failed = [line for line in text.splitlines()
          if "HID-I2C:" in line and ("refused" in line or "disabled until reboot" in line)]
if failed:
    print("Transport did not qualify:")
    print("\n".join(failed[-3:]))
    raise SystemExit(1)
required = ["native input ready", "RESET complete (zero length)",
            "report descriptor fetched", "precision-verified=1", "HID-I2C rate: complete"]
missing = [item for item in required if item not in text]
if missing:
    raise SystemExit("Incomplete qualification: " + ", ".join(missing))


def fields(kind, phase):
    match = re.search(r"HID-I2C " + kind + r": phase=" + phase + r" ([^\n\r]*)", text)
    if not match:
        raise SystemExit("Missing " + kind + " for " + phase)
    return dict(re.findall(r"([a-z-]+)=([^ ]+)", match[1]))


print("GPIO/reset/descriptor/mode verified; finite sweep complete.")
print("ms frames delivered/s read-gap(ms) poll-gap(ms) service min/med/max(us) estimate(%)")
for interval in [8, 4, 2, 1]:
    phase = "move-" + str(interval)
    rate, cost = fields("rate", phase), fields("cost", phase)
    spacing, polls = fields("spacing", phase), fields("polls", phase)
    count, span = int(rate["frames"]), int(rate["observed-report-span-ns"])
    if count < 2 or not span or int(rate["malformed"]):
        raise SystemExit("Insufficient or malformed moving input at " + phase)
    if cost["clock"] != "TSC" or cost["cost-valid"] != "1":
        raise SystemExit("Native TSC service estimate unavailable at " + phase)
    delivered = (count - 1) * 1e9 / span
    gap = int(spacing["read-spacing-ns"].split("/")[1]) / 1e6
    poll_gap = int(polls["actual-spacing-ns"].split("/")[1]) / 1e6
    services = "/".join(f"{int(value) / 1000:.1f}" for value in cost["service-ns"].split("/"))
    fraction = int(cost["estimated-fraction-ppm"]) / 10000
    print(f"{interval:2} {count:6} {delivered:11.1f} {gap:12.3f} {poll_gap:12.3f} "
          f"{services:24} {fraction:.3f}")
print("Host-delivered rate; no sequence-free loss guarantee. Estimate excludes scheduler/cache effects.")
