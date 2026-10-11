#!/usr/bin/env bash
# Fetch the community RE references into reference/ (git-ignored). Re-run to update;
# pin CommonLibSSE by setting COMMONLIB_REF to a commit.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
mkdir -p "$ROOT/reference/AddressLibraryDatabase"

if [[ ! -d "$ROOT/reference/CommonLibSSE/.git" ]]; then
    git clone -q https://github.com/powerof3/CommonLibSSE.git "$ROOT/reference/CommonLibSSE"
fi
git -C "$ROOT/reference/CommonLibSSE" fetch -q origin
git -C "$ROOT/reference/CommonLibSSE" checkout -q "${COMMONLIB_REF:-origin/HEAD}"
echo "CommonLibSSE: $(git -C "$ROOT/reference/CommonLibSSE" log -1 --format='%h %cs %s')"

curl -sfL -o "$ROOT/reference/AddressLibraryDatabase/skyrimae.rename" \
    https://raw.githubusercontent.com/meh321/AddressLibraryDatabase/main/skyrimae.rename
echo "meh321 names: $(wc -l < "$ROOT/reference/AddressLibraryDatabase/skyrimae.rename") entries"

GAME="${SKYRIM_GAME_DIR:-/media/bighass/1f640250-798d-4f2f-87f2-c442109fc5d0/SteamLibrary/steamapps/common/Skyrim Special Edition}"
[[ -d "$GAME/src/skse64" ]] && echo "SKSE source: $GAME/src/skse64" \
    || echo "SKSE source missing: extract the SKSE 2.2.6 archive's src/ into $GAME"
