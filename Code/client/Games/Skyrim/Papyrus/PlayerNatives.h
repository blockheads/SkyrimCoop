#pragma once

#include <Misc/BSScript.h>
#include <Structs/ScriptValue.h>

#include <cstdint>
#include <optional>

// The calls PartyNatives::Scope sends to one other game: on the host, a native acting on "the player" in a script run
// for a guest goes to that guest's game (docs/design/party-model.md, rule 2; docs/re/systems/papyrus-guest-scripts.md,
// "Natives on the player"); on a guest, its own script's call changing the world (a quest's stage) to the host's.
namespace PlayerNatives
{
// On while this game is a guest in a session (GuestService).
void SetGuest(bool aIsGuest) noexcept;
[[nodiscard]] bool IsGuest() noexcept;
// A guest's own script changed the world (a quest's stage): the host's game makes acCall (RoutedCallEvent), and it
// completes here (a bool one with true, as the host takes it). Sent as a RoutedCallEvent.
[[nodiscard]] BSScript::CallResult SendToHost(BSScript::IFunction* apFunction, BSScript::Stack& aStack, LocalCall aCall) noexcept;
// A native on the player in a stack run for aGuest (a form id): its game runs acCall on its own player
// (RoutedCallEvent). A void one completes; one returning a value waits for that game's answer.
[[nodiscard]] BSScript::CallResult SendToGuest(BSScript::IFunction* apFunction, BSScript::Stack& aStack, BSScript::IVirtualMachine* apVm,
                                               uint32_t aGuest, LocalCall aCall) noexcept;
// The guest's game answered the stack waiting on aReplyId: it goes on with acResult (None: its return type's default).
void Answer(uint32_t aReplyId, const LocalValue& acResult) noexcept;
// A stack ended: nothing waits for it any more.
void StackEnded(uint32_t aStackId) noexcept;

// On a guest, a quest its own player's scripts started (its beast form, a mod's per-player quest): the guest's, its
// stages and events stay on its game (docs/design/world-state-sync.md, "A guest's own quests").
[[nodiscard]] bool IsOwnQuest(uint32_t aFormId) noexcept;
} // namespace PlayerNatives
