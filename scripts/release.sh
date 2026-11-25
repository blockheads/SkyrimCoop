#!/usr/bin/env bash
# scripts/release.sh TAG: the package as a GitHub prerelease of HEAD (docs/design/launcher.md, Releases).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
TAG="${1:?usage: scripts/release.sh TAG}"
[[ -z "$(git status --porcelain)" ]] || { echo "commit first: the release must name a clean commit" >&2; exit 1; }
COMMIT="$(git rev-parse HEAD)"
git branch -r --contains "$COMMIT" | grep -q . || { echo "push $COMMIT first" >&2; exit 1; }
TAGGED="$(git ls-remote origin "refs/tags/$TAG" "refs/tags/$TAG^{}" | tail -1 | cut -f1)"
[[ -z "$TAGGED" || "$TAGGED" == "$COMMIT" ]] || { echo "tag $TAG already names $TAGGED, not $COMMIT" >&2; exit 1; }
scripts/package.sh
ZIP="$(ls -t build/package/SkyrimCoop-*.zip | head -1)"
gh release create "$TAG" "$ZIP" --target "$COMMIT" --prerelease --title "SkyrimCoop $TAG" --notes \
"Built from $COMMIT for Skyrim SE 1.7.104. An ordinary mod archive (its root is Data/); it needs SKSE, Address Library for SKSE Plugins, the Unofficial Skyrim Special Edition Patch and Alternate Start - Live Another Life. Steps: docs/install.md."
echo "released $TAG: $(basename "$ZIP")"
