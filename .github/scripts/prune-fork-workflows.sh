#!/usr/bin/env bash
# Keep only fork-owned GitHub Actions workflows; drop upstream's hundreds of yaml files.
set -euo pipefail

wf_dir=".github/workflows"
declare -a keep=(
  debian-build.yml
  rpm-build.yml
  sync-upstream.yml
  mirror-upstream-release.yml
  .gitignore
)

is_kept() {
  local name="$1"
  for k in "${keep[@]}"; do
    [[ "${name}" == "${k}" ]] && return 0
  done
  return 1
}

shopt -s nullglob
for path in "${wf_dir}"/*; do
  name=$(basename "${path}")
  if is_kept "${name}"; then
    continue
  fi
  if git ls-files --error-unmatch "${path}" &>/dev/null; then
    git rm -f --quiet "${path}"
  else
    rm -f "${path}"
  fi
done

echo "Fork workflows:"
ls -1 "${wf_dir}"
