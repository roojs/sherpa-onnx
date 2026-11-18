#!/usr/bin/env bash
# Auto-resolve conflicts the packaging fork expects on every upstream merge.
#
# Allowed:
#   .github/workflows/*  — deleted later by prune-fork-workflows.sh
#   README.md            — only when this fork's side is the upstream base
#                          plus the leading packaging banner. The banner is
#                          kept and the rest of the file is taken from upstream.
#
# Any other unmerged path, a merge failure with no unmerged files, or a
# README change beyond that banner exits 1. Does not abort the merge.
set -euo pipefail

mapfile -t conflicts < <(git diff --name-only --diff-filter=U)
if [[ ${#conflicts[@]} -eq 0 ]]; then
  echo "Merge failed with no unmerged files" >&2
  exit 1
fi

disallowed=()
readme=0
for f in "${conflicts[@]}"; do
  case "${f}" in
    .github/workflows/*) ;;
    README.md) readme=1 ;;
    *) disallowed+=("${f}") ;;
  esac
done

if [[ ${#disallowed[@]} -gt 0 ]]; then
  echo "Merge conflict outside auto-resolve (.github/workflows/* and the README fork banner):" >&2
  printf '  %s\n' "${conflicts[@]}" >&2
  exit 1
fi

if [[ "${readme}" -eq 0 ]]; then
  echo "Auto-resolving ${#conflicts[@]} workflow conflict(s)"
  exit 0
fi

tmp=$(mktemp -d)
trap 'rm -rf "${tmp}"' EXIT

git show :1:README.md > "${tmp}/base"
git show :2:README.md > "${tmp}/ours"
git show :3:README.md > "${tmp}/theirs"

# Banner is the leading fork notice, through the "---" separator and one
# trailing blank line when that blank line is present.
banner_len=$(awk '
  NR == 1 && $0 !~ /^> \*\*Fork notice:\*\*/ { exit 2 }
  $0 == "---" { seen = NR; next }
  seen {
    if ($0 == "") print NR
    else print seen
    exit 0
  }
  END { if (!seen) exit 2 }
' "${tmp}/ours") || {
  echo "README conflict is not the fork banner; refusing to auto-resolve" >&2
  exit 1
}

tail -n +"$((banner_len + 1))" "${tmp}/ours" > "${tmp}/ours_body"
if ! cmp -s "${tmp}/ours_body" "${tmp}/base"; then
  echo "README conflict includes fork edits beyond the packaging banner; refusing to auto-resolve" >&2
  exit 1
fi

if [[ "$(head -n 1 "${tmp}/theirs")" == "> **Fork notice:**"* ]]; then
  cp "${tmp}/theirs" README.md
else
  head -n "${banner_len}" "${tmp}/ours" > README.md
  cat "${tmp}/theirs" >> README.md
fi
git add README.md
echo "Auto-resolved README.md by keeping the fork banner and taking upstream"
echo "Auto-resolving ${#conflicts[@]} conflict(s)"
