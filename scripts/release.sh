#!/usr/bin/env bash
# Publishes the mod as a GitHub release of this repo: scripts/package.sh's archive (what hosts install in MO2, and
# what `sms host source SkyrimCoop github blockheads/SkyrimCoop` picks up), marked a prerelease. Needs a clean tree
# at a pushed commit, so the release names the code it was built from; the symbols stay local (build/package).
#   scripts/release.sh TAG        e.g. scripts/release.sh playtest-2026-11-25
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
TAG="${1:?usage: scripts/release.sh TAG}"
[[ -z "$(git status --porcelain --untracked-files=no)" ]] || { echo "commit first: the release must name a clean commit" >&2; exit 1; }
COMMIT="$(git rev-parse HEAD)"
git branch -r --contains "$COMMIT" | grep -q . || { echo "push $COMMIT first" >&2; exit 1; }
scripts/package.sh
ZIP="$(ls -t build/package/SkyrimCoop-*.zip | head -1)"
gh release create "$TAG" "$ZIP" --target "$COMMIT" --prerelease --title "SkyrimCoop $TAG" --notes \
"The mod for Skyrim SE 1.7.104, as one archive Mod Organizer 2 installs. Built from $COMMIT.

Hosts: install it in MO2 with SKSE, Address Library, USSEP and Alternate Start, then publish with [SkyrimModSync](https://github.com/blockheads/SkyrimModSync) (\`sms host source SkyrimCoop github blockheads/SkyrimCoop\`). Friends: \`sms play <code>\`. Steps: docs/install.md."
echo "released $TAG: $(basename "$ZIP")"
