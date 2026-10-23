#pragma once

#include <Misc/BSScript.h>
#include <Structs/ScriptValue.h>

#include <cstdint>
#include <optional>

// On the host, a native acting on "the player" in a script run for a guest: that guest's game runs it, on its own
// player (docs/design/party-model.md, rule 2; docs/re/systems/papyrus-guest-scripts.md, "Natives on the player").
// On a guest, its own script's call changing the world (a quest's stage) is the host's game's to make.
namespace PlayerNatives
{
// On while this game is a guest in a session (GuestService).
void SetGuest(bool aIsGuest) noexcept;
// On a guest, a native starting in a stack tagged aTag (GuestScripts): one changing the world (NativeReach::Call::kWorld)
// goes to the host (WorldCallEvent) and completes here (a bool one with true, as the host takes it). None: it runs here,
// as does one on a quest of the guest's own (IsOwnQuest).
[[nodiscard]] std::optional<BSScript::CallResult> SendToHost(BSScript::IFunction* apFunction, BSScript::Stack* apStack, uint32_t aTag) noexcept;

// A native starting in a stack run for aGuest (a form id): one on the player goes to the guest's game
// (PlayerNativeEvent), and the result is the call's: a void one completes, one returning a value waits for that
// game's answer. None: it runs here.
[[nodiscard]] std::optional<BSScript::CallResult> SendToGuest(BSScript::IFunction* apFunction, BSScript::Stack& aStack, BSScript::IVirtualMachine* apVm,
                                                              uint32_t aGuest) noexcept;
// The guest's game answered the stack waiting on aReplyId: it goes on with acResult (None: its return type's default).
void Answer(uint32_t aReplyId, const LocalValue& acResult) noexcept;
// A stack ended: nothing waits for it any more.
void StackEnded(uint32_t aStackId) noexcept;

// On a guest, a quest its own player's scripts started (its beast form, a mod's per-player quest): the guest's, its
// stages and events stay on its game (docs/design/world-state-sync.md, "A guest's own quests").
[[nodiscard]] bool IsOwnQuest(uint32_t aFormId) noexcept;
} // namespace PlayerNatives
