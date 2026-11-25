#!/usr/bin/env bash
# scripts/release.sh TAG: the package and its pack.json (playtest/modlist.json resolved by sms) as a GitHub prerelease of
# HEAD; players run `sms play github:blockheads/SkyrimCoop` (docs/design/launcher.md, Releases).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
TAG="${1:?usage: scripts/release.sh TAG}"
[[ -z "$(git status --porcelain)" ]] || { echo "commit first: the release must name a clean commit" >&2; exit 1; }
COMMIT="$(git rev-parse HEAD)"
git branch -r --contains "$COMMIT" | grep -q . || { echo "push $COMMIT first" >&2; exit 1; }
TAGGED="$(git ls-remote origin "refs/tags/$TAG" "refs/tags/$TAG^{}" | tail -1 | cut -f1)"
[[ -z "$TAGGED" || "$TAGGED" == "$COMMIT" ]] || { echo "tag $TAG already names $TAGGED, not $COMMIT" >&2; exit 1; }
SMS="${SKYRIM_MOD_SYNC:-$(command -v sms || true)}"
[[ -x "$SMS" ]] || { echo "sms not found: put SkyrimModSync's sms on PATH or set SKYRIM_MOD_SYNC" >&2; exit 1; }
# Fails early, before any release exists, if Nexus archives still need downloading by hand.
"$SMS" host publish playtest/modlist.json >/dev/null
scripts/package.sh
ZIP="$(ls -t build/package/SkyrimCoop-*.zip | head -1)"
gh release create "$TAG" "$ZIP" --target "$COMMIT" --prerelease --title "SkyrimCoop $TAG" --notes \
"Built from $COMMIT for Skyrim SE 1.7.104.

To play: get [SkyrimModSync](https://github.com/blockheads/SkyrimModSync/releases/latest), run \`sms setup\` once, then \`sms play github:blockheads/SkyrimCoop\`. It installs this build with SKSE, Address Library, USSEP and Alternate Start (playtest/modlist.json), and \`sms unload\` restores the game."
# The list's SkyrimCoop entry is this repo's newest release: the one just made.
"$SMS" host publish playtest/modlist.json --output build/package/pack.json
gh release upload "$TAG" build/package/pack.json
echo "released $TAG: $(basename "$ZIP") and pack.json"
