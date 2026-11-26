#pragma once

#include <scripts/PlayerNatives.h>

#include <Misc/BSScript.h>

// PlayerNatives' calls on the engine's own script types (sync sees scripts/PlayerNatives.h).
namespace PlayerNatives
{
// A guest's own script changed the world (a quest's stage): the host's game makes acCall (RoutedCallEvent), and it
// completes here (a bool one with true, as the host takes it). aReply (a created reference): it waits for the host's
// answer instead (Answer).
[[nodiscard]] BSScript::CallResult SendToHost(BSScript::IFunction* apFunction, BSScript::Stack& aStack, BSScript::IVirtualMachine* apVm,
                                              LocalCall aCall, bool aReply = false) noexcept;
// A native on the player in a stack run for aGuest (a form id): its game runs acCall on its own player
// (RoutedCallEvent). A void one completes; one returning a value waits for that game's answer.
[[nodiscard]] BSScript::CallResult SendToGuest(BSScript::IFunction* apFunction, BSScript::Stack& aStack, BSScript::IVirtualMachine* apVm,
                                               uint32_t aGuest, LocalCall aCall) noexcept;
} // namespace PlayerNatives
