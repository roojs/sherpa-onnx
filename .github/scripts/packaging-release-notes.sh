#!/usr/bin/env bash
# Build GitHub Release body for vX.Y.Z-roojsN tags: packaging byline + upstream notes.
set -euo pipefail

tag="${GITHUB_REF_NAME:?GITHUB_REF_NAME is required}"
out_file="${1:-release-notes.md}"
upstream_repo="k2-fsa/sherpa-onnx"
fork_repo="${GITHUB_REPOSITORY:?GITHUB_REPOSITORY is required}"

if [[ "${tag}" =~ ^v([0-9]+\.[0-9]+\.[0-9]+)(-roojs[0-9]+)?$ ]]; then
  upstream_tag="v${BASH_REMATCH[1]}"
else
  upstream_tag="${tag}"
fi

upstream_body=""
if command -v gh >/dev/null 2>&1; then
  upstream_body="$(
    gh release view "${upstream_tag}" \
      --repo "${upstream_repo}" \
      --json body \
      -q .body 2>/dev/null || true
  )"
fi

{
  cat <<EOF
> **Packaged release** \`${tag}\` — Debian \`.deb\` and Fedora RPM packages for the sherpa-onnx **C API** (ASR-trimmed build). Mirrors upstream [${upstream_tag}](https://github.com/${upstream_repo}/releases/tag/${upstream_tag}).
>
> Packaging fork: [${fork_repo}](https://github.com/${fork_repo})

---

EOF
  if [ -n "${upstream_body}" ]; then
    printf '%s\n' "${upstream_body}"
  else
    echo "_Upstream release notes for ${upstream_tag} were not found._"
  fi
} > "${out_file}"

echo "Wrote ${out_file} (upstream ${upstream_tag})"
