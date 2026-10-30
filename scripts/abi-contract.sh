#!/bin/sh

# Shared by generation and the ABI CI report. The comparison is textual,
# including comments: it does not prove layout or semantic compatibility.
abi_contract()
{
  abi_history=$(git rev-list --first-parent HEAD) || return 1
  abi_tags=$(git tag --list | LC_ALL=C grep -E '^0\.0\.(0|[1-9][0-9]*)[a-z]?$' || :)
  abi_release=
  abi_distance=
  abi_ambiguous=
  for abi_tag in $abi_tags; do
    abi_commit=$(git rev-parse --verify "refs/tags/$abi_tag^{commit}") || return 1
    abi_position=$(printf '%s\n' "$abi_history" | awk -v commit="$abi_commit" '$0 == commit { print NR; exit }')
    [ -n "$abi_position" ] || continue
    if [ -z "$abi_distance" ] || [ "$abi_position" -lt "$abi_distance" ]; then
      abi_release=$abi_tag
      abi_distance=$abi_position
      abi_ambiguous=
    elif [ "$abi_position" -eq "$abi_distance" ]; then
      abi_ambiguous="$abi_release and $abi_tag"
    fi
  done
  if [ -z "$abi_release" ]; then
    echo "abi-contract: no actual 0.0.x release tag on first-parent history; fetch release history and tags" >&2
    return 1
  fi

  if [ -n "$abi_ambiguous" ]; then
    echo "abi-contract: ambiguous release tags $abi_ambiguous at the nearest first-parent release" >&2
    return 1
  fi

  abi_patch=${abi_release#0.0.}
  abi_patch=${abi_patch%[a-z]}
  abi_base=0.0.$abi_patch
  abi_numstat=$(git diff --no-renames --numstat "$abi_release" -- include/abi) || return 1
  abi_removed=$(printf '%s\n' "$abi_numstat" | awk '$2 != 0 { print $3 }')
  abi_compatible=false
  if [ "$abi_patch" -lt 11 ]; then
    # Released static metadata/command readers first support dev ABIs in 0.0.11.
    abi_base=0.0.11
  elif [ -z "$abi_removed" ]; then
    abi_compatible=true
  else
    abi_base=0.0.$((abi_patch + 1))
  fi
}
