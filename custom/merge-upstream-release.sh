#!/usr/bin/env bash
# Merge an upstream Fincept release tag into the `custom` branch.
#
# Usage: custom/merge-upstream-release.sh [tag]
#   tag defaults to the latest upstream GitHub release (falls back to the
#   highest v* tag on the upstream remote when `gh` is unavailable).
#
# On a clean merge the script records the tag in custom/UPSTREAM_BASE and
# commits. On conflicts it stops and prints the steps to finish by hand.
set -euo pipefail

UPSTREAM_REPO="Fincept-Corporation/FinceptTerminal"
UPSTREAM_URL="https://github.com/${UPSTREAM_REPO}.git"

cd "$(git rev-parse --show-toplevel)"

if [ "$(git rev-parse --abbrev-ref HEAD)" != "custom" ]; then
  echo "error: check out the custom branch first" >&2
  exit 1
fi
if [ -n "$(git status --porcelain)" ]; then
  echo "error: working tree is not clean" >&2
  exit 1
fi

git remote get-url upstream >/dev/null 2>&1 || git remote add upstream "$UPSTREAM_URL"
git fetch upstream --tags

tag="${1:-}"
if [ -z "$tag" ]; then
  if command -v gh >/dev/null 2>&1; then
    tag="$(gh release view --repo "$UPSTREAM_REPO" --json tagName -q .tagName)"
  else
    tag="$(git ls-remote --tags --refs upstream 'v*' | sed 's#.*/##' | sort -V | tail -1)"
  fi
fi

base="$(tr -d '[:space:]' < custom/UPSTREAM_BASE)"
if [ "$tag" = "$base" ]; then
  echo "custom is already based on $tag"
  exit 0
fi

echo "Merging upstream $tag into custom (current base: $base)"
if git merge --no-ff --no-commit "$tag"; then
  printf '%s\n' "$tag" > custom/UPSTREAM_BASE
  git add custom/UPSTREAM_BASE
  git commit -m "Merge upstream release $tag into custom"
  echo "Done. Review the build, then push: git push origin custom"
else
  cat >&2 <<EOF

Merge stopped on conflicts. To finish:
  1. Resolve the conflicted files (git status lists them).
  2. printf '%s\n' "$tag" > custom/UPSTREAM_BASE
  3. git add -A && git commit -m "Merge upstream release $tag into custom"
To abort instead: git merge --abort
EOF
  exit 1
fi
