#!/bin/sh
set -eu

. ./scripts/abi-contract.sh
abi_contract
if [ -n "$abi_removed" ]; then
  echo "check-abi: include/abi/ removes or changes lines of $abi_release:"
  printf '%s\n' "$abi_removed"
else
  echo "check-abi: include/abi/ keeps every line of $abi_release"
fi
echo "check-abi: generated build contract:"
exec ./scripts/os-release.sh
