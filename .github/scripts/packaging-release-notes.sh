#!/usr/bin/env bash
# Build GitHub Release body for vX.Y.Z-roojsN tags: packaging byline + upstream notes.
set -euo pipefail

tag="${GITHUB_REF_NAME:?GITHUB_REF_NAME is required}"
out_file="${1:-release-notes.md}"
upstream_repo="k2-fsa/sherpa-onnx"
fork_repo="${GITHUB_REPOSITORY:?GITHUB_REPOSITORY is required}"
changelog_file="${CHANGELOG_FILE:-CHANGELOG.md}"

if [[ "${tag}" =~ ^v([0-9]+\.[0-9]+\.[0-9]+)(-roojs[0-9]+)?$ ]]; then
  upstream_tag="v${BASH_REMATCH[1]}"
  upstream_version="${BASH_REMATCH[1]}"
else
  upstream_tag="${tag}"
  upstream_version="${tag#v}"
fi

changelog_section() {
  local version="$1"
  local file="$2"
  [ -f "${file}" ] || return 1

  awk -v ver="${version}" '
    $0 ~ "^## " ver "$" { found=1; next }
    found && /^## / { exit }
    found { print }
  ' "${file}" | sed '/./,$!d'
}

generated_upstream_notes() {
  command -v gh >/dev/null 2>&1 || return 1
  gh api \
    --method POST \
    "/repos/${upstream_repo}/releases/generate-notes" \
    -f "tag_name=${upstream_tag}" \
    -f "target_commitish=${upstream_tag}" \
    -q .body 2>/dev/null || true
}

upstream_body=""
notes_source=""

if command -v gh >/dev/null 2>&1; then
  upstream_body="$(
    gh release view "${upstream_tag}" \
      --repo "${upstream_repo}" \
      --json body \
      -q .body 2>/dev/null || true
  )"
  if [ -n "${upstream_body}" ]; then
    notes_source="upstream GitHub release"
  fi
fi

if [ -z "${upstream_body}" ]; then
  changelog_body="$(changelog_section "${upstream_version}" "${changelog_file}" || true)"
  if [ -n "${changelog_body}" ]; then
    upstream_body="${changelog_body}"
    notes_source="CHANGELOG.md"
  fi
fi

if [ -z "${upstream_body}" ]; then
  generated_body="$(generated_upstream_notes)"
  if [ -n "${generated_body}" ]; then
    upstream_body="${generated_body}"
    notes_source="GitHub generate-notes API"
  fi
fi

{
  cat <<EOF
> **Packaged release** \`${tag}\` — Debian \`.deb\` and Fedora RPM packages for the sherpa-onnx **C API** (ASR-trimmed build). Mirrors upstream [${upstream_tag}](https://github.com/${upstream_repo}/releases/tag/${upstream_tag}).
>
> Packaging fork: [${fork_repo}](https://github.com/${fork_repo})

---

EOF
  if [ -n "${upstream_body}" ]; then
    if [ "${notes_source}" = "CHANGELOG.md" ]; then
      cat <<EOF
### Upstream changes (from CHANGELOG.md)

EOF
    fi
    printf '%s\n' "${upstream_body}"
  else
    echo "_Upstream release notes for ${upstream_tag} were not found (no GitHub release body, CHANGELOG.md section, or generated notes)._"
  fi
} > "${out_file}"

if [ -n "${notes_source}" ]; then
  echo "Wrote ${out_file} (upstream ${upstream_tag}, source: ${notes_source})"
else
  echo "Wrote ${out_file} (upstream ${upstream_tag}, no upstream notes found)"
fi
