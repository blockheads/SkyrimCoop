#pragma once

#include <Structs/ScriptValue.h>

#include <cstdint>
#include <optional>

// The engine's half is Papyrus/PlayerNativeCalls.h (SendToHost, SendToGuest).
// The calls PartyNatives::Scope sends to one other game: on the host, a native acting on "the player" in a script run
// for a guest goes to that guest's game (docs/design/party-model.md, rule 2; docs/re/systems/papyrus-guest-scripts.md,
// "Natives on the player"); on a guest, its own script's call changing the world (a quest's stage) to the host's.
namespace PlayerNatives
{
// On while this game is a guest in a session (GuestService).
void SetGuest(bool aIsGuest) noexcept;
[[nodiscard]] bool IsGuest() noexcept;
// The game asked answered the stack waiting on aReplyId: it goes on with acResult (None: its return type's default).
void Answer(uint32_t aReplyId, const LocalValue& acResult) noexcept;
// This game left the session: every script waiting on another game goes on with None.
void AnswerAll() noexcept;
// A stack ended: nothing waits for it any more.
void StackEnded(uint32_t aStackId) noexcept;

// On a guest, a quest its own player's scripts started (its beast form, a mod's per-player quest): the guest's, its
// stages and events stay on its game (docs/design/world-state-sync.md, "A guest's own quests").
[[nodiscard]] bool IsOwnQuest(uint32_t aFormId) noexcept;
} // namespace PlayerNatives
