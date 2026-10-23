#pragma once

#include <Papyrus/PartyRules.h>
#include <Structs/ScriptValue.h>

#include <cstdint>
#include <optional>

// The screen is the party's in a cutscene: a script's call taking or giving back the player's movement, or fading,
// moving the camera, shaking, playing music or a video, runs on every game, each on its own player and screen. It is
// the party's when no player's action is behind the script, or a stage that action set runs it (the story); otherwise
// it is that player's moment and stays theirs (docs/design/cutscenes.md, "Whose moment"). Which calls they are is read
// from their code (NativeReach::CallOf). The call itself is caught by Scope (PartyNativesScope.h).
namespace PartyNatives
{
// A stack tagged so runs another game's party call: here only (PartyRules).
using PartyRules::IsMirrored;

// On while this game is in a session (CutsceneService): outside one, nothing is the party's.
void SetInSession(bool aInSession) noexcept;
[[nodiscard]] bool InSession() noexcept;

// Runs another game's party call here, on this game's player and screen; aSource is that game's player id (its stack
// is tagged mirrored from it). False: not queued.
bool Run(const LocalCall& acCall, uint32_t aSource) noexcept;

// What a game's cutscene still holds here comes back (Game.EnablePlayerControls, for what no one else holds): that
// player left (aSource), or the session ended (none: every other game's).
void GiveBackControls(std::optional<uint32_t> aSource = std::nullopt) noexcept;
} // namespace PartyNatives
