#!/usr/bin/env bash
# Fetch the community RE references into reference/ (git-ignored). Re-run to update;
# pin CommonLibSSE by setting COMMONLIB_REF to a commit.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
mkdir -p "$ROOT/reference/AddressLibraryDatabase" "$ROOT/reference/plugins"

# Clones (shallow unless full history is wanted) or updates a GitHub repo at a ref.
fetch() {
    local repo="$1" dir="$2" ref="${3:-}"
    if [[ ! -d "$dir/.git" ]]; then
        git clone -q --depth 1 "https://github.com/$repo.git" "$dir"
    fi
    git -C "$dir" fetch -q --depth 1 origin ${ref:+"$ref"}
    git -C "$dir" checkout -q FETCH_HEAD
    echo "$repo: $(git -C "$dir" log -1 --format='%h %cs %s')"
}

# CommonLibSSE: engine classes, layouts, vtables, RTTI, RELOCATION_ID(SE, AE). powerof3's fork
# first; alandtse's NG has ~170 more AE IDs and some layout fixes (the ButtonEvent stride, PR #347),
# plus helpers powerof3's lacks (ButtonEvent::Create).
fetch powerof3/CommonLibSSE "$ROOT/reference/CommonLibSSE" "${COMMONLIB_REF:-}"
fetch alandtse/CommonLibSSE-NG "$ROOT/reference/CommonLibSSE-NG" "${COMMONLIB_NG_REF:-}"

# SKSE plugins that hook or call engine code we also touch: where someone else hooked an address
# and how. build_re_data.py adds "used by" lines to the annotations.
for repo in ersh1/Precision ersh1/TrueHUD aers/EngineFixesSkyrim64 powerof3/po3-Tweaks \
            powerof3/PapyrusExtenderSSE; do
    fetch "$repo" "$ROOT/reference/plugins/${repo#*/}"
done

# SkyMP: the other open-source Skyrim multiplayer. How it shows remote actors: engine AI left on,
# moved with keepOffsetFromActor/TranslateTo (skymp5-client/src/sync/movementApply.ts), hands forced by
# overriding DrawWeaponMagicHands' argument (skyrim-platform/.../FridaHooks.cpp). See
# docs/re/systems/actor-ai-and-copies.md.
fetch skyrim-multiplayer/skymp "$ROOT/reference/plugins/skymp"

# Plugins that hook Character::UpdateAI's essential/protected upkeep (CalculateEssentialProtected, 37227) and
# its essential check (docs/re/systems/actor-ai-and-copies.md, the audit of UpdateAI's callees).
for repo in powerof3/EssentialsBeGone NoahBoddie/death-defiance-system; do
    fetch "$repo" "$ROOT/reference/plugins/${repo#*/}"
done

# Actor values: ActorValueGenerator hooks the actor value owner's base setter and 38468's modifiers.
fetch NoahBoddie/ActorValueGenerator "$ROOT/reference/plugins/ActorValueGenerator"

# Kill moves: Kaputt plays paired kill-move idles (AIProcess play-paired-idle 39256) and hooks the attack action
# where vanilla picks one (49170+0x435) and the hit (38627+0x4a8).
fetch Pentalimbed/kaputt "$ROOT/reference/plugins/kaputt"

# Behaviour graph internals: Open Animation Replacer walks every node of a behaviour with the engine's own
# iterator (62942/62944, src/Offsets.h) and hooks project loading. hkxcmd's compat/ has the Havok 2010
# class layouts (hkbVariableBindingSet::Binding, FLAG_OUTPUT). docs/design/graph-replication.md.
fetch ersh1/OpenAnimationReplacer "$ROOT/reference/plugins/OpenAnimationReplacer"
fetch figment/hkxcmd "$ROOT/reference/plugins/hkxcmd"

# Dialogue (shared dialogue, docs/re/systems/dialogue.md): the vanilla DialogueMenu ActionScript (its list
# and TopicClicked callbacks), a plugin that hooks DialogueMenu::Accept and swaps TopicClicked, and plugins
# that drive MenuTopicManager (skipping lines, the topic list); moving during a conversation (its input
# remap, the skipped turn to the speaker) and unpaused menus; a plugin that hides, replaces and adds topics
# while the engine builds the list (MenuTopicManager::AddTopic).
for repo in Mardoxx/skyrimui DCCStudios/CHIM-Dialogue-TTS-Bridge JonathanFeenstra/InstantlySkipDialogueNG \
            Seb263/SkyrimSE_SmartTalk Vermunds/DialogueMovementEnabler-SE Vermunds/SkyrimSoulsRE \
            KrisV-777/Dynamic-Dialogue-Replacer; do
    fetch "$repo" "$ROOT/reference/plugins/${repo#*/}"
done

# Camera and controls (docs/research/party-freezes.md): how mods read "the player is taken over" with no cutscene
# state, from the player's own state: movement controls enabled, AI-driven, the furniture camera.
for repo in mwilsnd/SkyrimSE-SmoothCam ersh1/TrueDirectionalMovement; do
    fetch "$repo" "$ROOT/reference/plugins/${repo#*/}"
done

curl -sfL -o "$ROOT/reference/AddressLibraryDatabase/skyrimae.rename" \
    https://raw.githubusercontent.com/meh321/AddressLibraryDatabase/main/skyrimae.rename
echo "meh321 names: $(wc -l < "$ROOT/reference/AddressLibraryDatabase/skyrimae.rename") entries"
# Every ID's offset in each AE version (1.6.317 on): turns an old hardcoded AE address into its ID
# (scripts/re/relib.py).
curl -sfL -o "$ROOT/reference/AddressLibraryDatabase/skyrimae.relib" \
    https://raw.githubusercontent.com/meh321/AddressLibraryDatabase/main/skyrimae.relib

GAME="${SKYRIM_GAME_DIR:-/media/bighass/1f640250-798d-4f2f-87f2-c442109fc5d0/SteamLibrary/steamapps/common/Skyrim Special Edition}"
[[ -d "$GAME/src/skse64" ]] && echo "SKSE source: $GAME/src/skse64" \
    || echo "SKSE source missing: extract the SKSE 2.2.6 archive's src/ into $GAME"
