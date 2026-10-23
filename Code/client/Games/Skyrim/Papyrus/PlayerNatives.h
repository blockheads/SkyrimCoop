#pragma once

#include <Misc/BSScript.h>
#include <Structs/ScriptValue.h>

#include <cstdint>
#include <optional>

// On the host, a native acting on "the player" in a script run for a guest: that guest's game runs it, on its own
// player (docs/design/party-model.md, rule 2; docs/re/systems/papyrus-guest-scripts.md, "Natives on the player").
namespace PlayerNatives
{
// A native starting in a stack run for aGuest (a form id): one on the player goes to the guest's game
// (PlayerNativeEvent), and the result is the call's: a void one completes, one returning a value waits for that
// game's answer. None: it runs here.
[[nodiscard]] std::optional<BSScript::CallResult> SendToGuest(BSScript::IFunction* apFunction, BSScript::Stack& aStack, BSScript::IVirtualMachine* apVm,
                                                              uint32_t aGuest) noexcept;
// The guest's game answered the stack waiting on aReplyId: it goes on with acResult (None: its return type's default).
void Answer(uint32_t aReplyId, const LocalValue& acResult) noexcept;
// A stack ended: nothing waits for it any more.
void StackEnded(uint32_t aStackId) noexcept;
} // namespace PlayerNatives
