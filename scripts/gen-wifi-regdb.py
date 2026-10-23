#!/usr/bin/env python3
"""Generate the bounded native 2.4 GHz rules from the pinned local archive."""

import argparse
import hashlib
import importlib.util
import io
import json
import math
from pathlib import Path
import re
import sys
import tarfile


ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "regulatory/wireless-regdb"
ARCHIVE_SHA256 = "b22e0901227b820cd1c280abe681a15b773a5103a5e10dc442e94ebb34cbf58d"
MAX_COUNTRIES = 256
MAX_RULES = 1024
MAX_COUNTRY_RULES = 16
CHANNEL_CENTERS_KHZ = tuple(2407000 + channel * 5000 for channel in range(1, 14))
CHANNEL_BANDWIDTH_KHZ = 20000
FLAGS = {
  "NO-OFDM": 1 << 0,
  "NO-CCK": 1 << 1,
  "NO-INDOOR": 1 << 2,
  "NO-OUTDOOR": 1 << 3,
  "DFS": 1 << 4,
  "PTP-ONLY": 1 << 5,
  "PTMP-ONLY": 1 << 6,
  "NO-IR": 1 << 7,
  "NO-HT40": 1 << 10,
  "AUTO-BW": 1 << 11,
}


def archive_member(archive, prefix, name):
  members = [member for member in archive.getmembers() if member.name == prefix + name]
  if len(members) != 1 or not members[0].isfile():
    raise ValueError(f"archive must contain exactly one regular {prefix + name}")
  with archive.extractfile(members[0]) as source:
    return source.read()


def load_database(archive_path):
  metadata = json.loads((SOURCE / "metadata.json").read_text(encoding="utf-8"))
  if metadata["archive_sha256"] != ARCHIVE_SHA256:
    raise ValueError("metadata does not match the pinned archive digest")
  with archive_path.open("rb") as source:
    digest = hashlib.file_digest(source, "sha256").hexdigest()
    if digest != ARCHIVE_SHA256:
      raise ValueError(f"archive SHA-256 mismatch: {digest}")
    source.seek(0)
    with tarfile.open(fileobj=source, mode="r:xz") as archive:
      prefix = f"wireless-regdb-{metadata['version']}/"
      database = archive_member(archive, prefix, "db.txt")
      parser_source = archive_member(archive, prefix, "dbparse.py")
      license_text = archive_member(archive, prefix, "LICENSE").decode("utf-8")
  if hashlib.sha256(database).hexdigest() != metadata["db_txt_sha256"]:
    raise ValueError("db.txt digest does not match metadata")
  if hashlib.sha256(parser_source).hexdigest() != metadata["upstream_dbparse_sha256"]:
    raise ValueError("upstream dbparse.py digest does not match metadata")
  if license_text.rstrip() != (SOURCE / "LICENSE").read_text(encoding="utf-8").rstrip():
    raise ValueError("retained ISC license does not match archive")

  spec = importlib.util.spec_from_file_location("wireless_regdb_dbparse", SOURCE / "dbparse.py")
  parser = importlib.util.module_from_spec(spec)
  sys.dont_write_bytecode = True
  spec.loader.exec_module(parser)
  if parser.flag_definitions != FLAGS:
    raise ValueError("parser flags do not match the represented native flags")

  def reject_warning(message):
    raise ValueError(message.rstrip())

  try:
    countries = parser.DBParser(warn=reject_warning).parse(io.StringIO(database.decode("utf-8")))
  except parser.SyntaxError as error:
    raise ValueError(str(error)) from error
  if b"00" not in countries or not 0 < len(countries) <= MAX_COUNTRIES:
    raise ValueError("world domain missing or country capacity exceeded")
  return countries, license_text, metadata


def khz(value):
  scaled = value * 1000
  if not math.isfinite(scaled) or scaled != int(scaled) or not 0 < scaled <= 0xffffffff:
    raise ValueError(f"frequency/bandwidth cannot be represented in integer kHz: {value}")
  return int(scaled)


def native_rule(permission):
  band = permission.freqband
  start, end, bandwidth = khz(band.start), khz(band.end), khz(band.maxbw)
  if start >= end or bandwidth > end - start:
    raise ValueError("invalid source frequency range or bandwidth")
  half_width = CHANNEL_BANDWIDTH_KHZ // 2
  if bandwidth < CHANNEL_BANDWIDTH_KHZ or not any(
      start <= center - half_width and center + half_width <= end
      for center in CHANNEL_CENTERS_KHZ):
    return None
  if permission.wmmrule is not None:
    raise ValueError("relevant 2.4 GHz WMM restriction is not represented")
  if permission.flags & ~sum(FLAGS.values()):
    raise ValueError("unrepresented source rule flags")
  eirp = permission.power.max_eirp * 100
  if not math.isfinite(eirp) or not -0x80000000 <= eirp <= 0x7fffffff:
    raise ValueError("EIRP cannot be represented in signed mBm")
  # The pinned grammar only accepts EIRP. Its zero gain means unspecified.
  if permission.power.max_ant_gain != 0:
    raise ValueError("unexpected supplied antenna-gain restriction")
  flags = ["WIFI_REGDB_" + name.replace("-", "_")
           for name, bit in FLAGS.items() if permission.flags & bit]
  return start, end, bandwidth, int(eirp), " | ".join(flags) or "0"


def generate(countries, license_text, metadata):
  rules = []
  index = []
  for alpha2, country in sorted(countries.items()):
    name = alpha2.decode("ascii")
    if re.fullmatch(r"00|[A-Z]{2}", name) is None:
      raise ValueError(f"unrepresented country identifier: {name}")
    first = len(rules)
    for permission in country.permissions:
      rule = native_rule(permission)
      if rule is not None:
        rules.append(rule)
    count = len(rules) - first
    if count > MAX_COUNTRY_RULES or len(rules) > MAX_RULES:
      raise ValueError(f"native rule capacity exceeded for {name}")
    index.append((name, first, count))

  lines = ["/*", " * Generated by scripts/gen-wifi-regdb.py; do not edit.",
           f" * wireless-regdb {metadata['version']}; SHA-256 {ARCHIVE_SHA256}",
           " * Legacy 2.4 GHz channels 1-13, 20 MHz occupied bandwidth only.",
           " * Rules retain upstream country.permissions order; never merge flags.",
           " * Antenna gain is unspecified in this source; has_antenna_gain=false.",
           " * Full provenance and parser changes: regulatory/wireless-regdb/metadata.json.",
           " *"]
  lines.extend(" * " + line if line else " *" for line in license_text.splitlines())
  lines.extend([" */", "", "enum {",
                f"  WIFI_REGDB_COUNTRY_COUNT = {len(index)},",
                f"  WIFI_REGDB_RULE_COUNT = {len(rules)},",
                "};", "", "static const struct wifi_regdb_rule wifi_regdb_rules[] = {"])
  for start, end, bandwidth, eirp, flags in rules:
    lines.append(f"  {{{start}, {end}, {bandwidth}, {eirp}, 0, {flags}, false}},")
  lines.extend(["};", "", "static const struct wifi_regdb_country wifi_regdb_countries[] = {"])
  for name, first, count in index:
    lines.append(f"  {{{{'{name[0]}', '{name[1]}'}}, {first}, {count}}},")
  lines.extend(["};", ""])
  return "\n".join(lines)


def main():
  arguments = argparse.ArgumentParser(description=__doc__)
  arguments.add_argument("--archive", required=True, type=Path,
                         help="local wireless-regdb-2026.09.03.tar.xz; never downloaded")
  arguments.add_argument("--output", type=Path, default=ROOT / "kernel/wifi/regdb_data.inc")
  args = arguments.parse_args()
  try:
    countries, license_text, metadata = load_database(args.archive)
    output = generate(countries, license_text, metadata)
    args.output.write_text(output, encoding="utf-8")
  except (OSError, ValueError, tarfile.TarError) as error:
    arguments.exit(1, f"gen-wifi-regdb: {error}\n")


if __name__ == "__main__":
  main()
