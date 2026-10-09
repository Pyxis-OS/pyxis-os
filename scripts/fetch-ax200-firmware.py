#!/usr/bin/env python3
"""Fetch the pinned AX200 assets from the owner's mirror only."""

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import sys
import tempfile
import urllib.error
import urllib.parse
import urllib.request


NETWORK_TIMEOUT_SECONDS = 30
READ_CHUNK_SIZE = 65536
PROVENANCE_NAME = "PROVENANCE.json"


class NoRedirect(urllib.request.HTTPRedirectHandler):
  def redirect_request(self, request, response, code, message, headers, new_url):
    return None


def read_metadata(path):
  with path.open(encoding="utf-8") as source:
    metadata = json.load(source)
  if not isinstance(metadata, dict):
    raise ValueError("metadata must be a JSON object")
  mirror_root = metadata.get("mirror_root")
  if not isinstance(mirror_root, str) or not mirror_root:
    raise ValueError("AX200 owner mirror_root is missing in the metadata")
  mirror = urllib.parse.urlsplit(mirror_root)
  if (mirror.scheme not in ("http", "https") or not mirror.hostname or
      mirror.username is not None or mirror.password is not None or
      mirror.query or mirror.fragment):
    raise ValueError("mirror_root must be an HTTP(S) prefix without credentials, query or fragment")
  files = metadata.get("files")
  if not isinstance(files, list) or not files:
    raise ValueError("metadata must list the AX200 assets")
  seen_paths = set()
  for asset in files:
    if not isinstance(asset, dict):
      raise ValueError("each metadata asset must be an object")
    name = asset.get("path")
    if not isinstance(name, str) or not name:
      raise ValueError("asset path must be a canonical relative path")
    relative = PurePosixPath(name)
    if (not relative.parts or relative.is_absolute() or ".." in relative.parts or
        str(relative) != name or "\\" in name or
        urllib.parse.quote(name, safe="/") != name or
        name == PROVENANCE_NAME or name in seen_paths):
      raise ValueError("asset path must be unique, canonical and relative")
    seen_paths.add(name)
    size = asset.get("size")
    if type(size) is not int or size <= 0:
      raise ValueError(f"{name}: asset size must be a positive integer")
    digest = asset.get("sha256")
    if not isinstance(digest, str) or re.fullmatch(r"[0-9a-f]{64}", digest) is None:
      raise ValueError(f"{name}: asset SHA-256 must be 64 lowercase hexadecimal digits")
  return metadata, mirror_root.rstrip("/") + "/"


def cache_matches(path, asset):
  try:
    with path.open("rb") as source:
      digest = hashlib.sha256()
      size = 0
      while size <= asset["size"]:
        data = source.read(min(READ_CHUNK_SIZE, asset["size"] + 1 - size))
        if not data:
          break
        size += len(data)
        digest.update(data)
      return size == asset["size"] and digest.hexdigest() == asset["sha256"]
  except FileNotFoundError:
    return False


def fetch_asset(opener, mirror_root, output, asset):
  name = asset["path"]
  destination = output / name
  if cache_matches(destination, asset):
    print(f"AX200 cached: {name}")
    return
  destination.parent.mkdir(parents=True, exist_ok=True)
  temporary = None
  try:
    with tempfile.NamedTemporaryFile(dir=destination.parent, prefix=destination.name + ".",
                                     suffix=".part", delete=False) as target:
      temporary = Path(target.name)
      digest = hashlib.sha256()
      size = 0
      try:
        with opener.open(mirror_root + name, timeout=NETWORK_TIMEOUT_SECONDS) as source:
          while size <= asset["size"]:
            data = source.read(min(READ_CHUNK_SIZE, asset["size"] + 1 - size))
            if not data:
              break
            size += len(data)
            digest.update(data)
            target.write(data)
      except urllib.error.HTTPError as error:
        raise ValueError(f"{name}: mirror returned HTTP {error.code}; redirects are not allowed") from None
      except urllib.error.URLError as error:
        raise ValueError(f"{name}: mirror request failed ({type(error.reason).__name__})") from None
      if size != asset["size"]:
        raise ValueError(f"{name}: size mismatch (expected {asset['size']}, received {size})")
      if digest.hexdigest() != asset["sha256"]:
        raise ValueError(f"{name}: SHA-256 mismatch")
      target.flush()
      os.fchmod(target.fileno(), 0o644)
    os.replace(temporary, destination)
    print(f"AX200 fetched: {name}")
  finally:
    if temporary is not None:
      temporary.unlink(missing_ok=True)


def write_provenance(output, metadata):
  temporary = None
  try:
    with tempfile.NamedTemporaryFile(dir=output, prefix=PROVENANCE_NAME + ".",
                                     suffix=".part", mode="w", encoding="utf-8",
                                     delete=False) as target:
      temporary = Path(target.name)
      json.dump(metadata, target, indent=2)
      target.write("\n")
      target.flush()
      os.fchmod(target.fileno(), 0o644)
    os.replace(temporary, output / PROVENANCE_NAME)
  finally:
    if temporary is not None:
      temporary.unlink(missing_ok=True)


def main():
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("--metadata", required=True, type=Path)
  parser.add_argument("--output", required=True, type=Path)
  args = parser.parse_args()
  try:
    metadata, mirror_root = read_metadata(args.metadata)
    opener = urllib.request.build_opener(NoRedirect())
    for asset in metadata["files"]:
      fetch_asset(opener, mirror_root, args.output, asset)
    write_provenance(args.output, metadata)
  except (OSError, ValueError) as error:
    print(f"AX200 firmware: {error}", file=sys.stderr)
    return 1
  return 0


if __name__ == "__main__":
  sys.exit(main())
