#pragma once

#include <Misc/BSScript.h>
#include <Structs/ScriptValue.h>

#include <functional>
#include <optional>
#include <span>
#include <string_view>

// Calling a global Papyrus function from our code, as a script's call does, and script values both ways.
namespace GlobalCall
{
// Told on a script thread what a queued call returned (None also for one the session cannot carry).
using OnReturn = std::function<void(LocalValue)>;

// Queues the call on the VM (IVirtualMachine::DispatchStaticCall, slot 0x26). False: not queued.
bool Queue(std::string_view aClass, std::string_view aFunction, std::span<const LocalValue> aArguments, OnReturn aOnReturn = {}) noexcept;

// A script's value as ours, and ours into a script's (a form not here: None). Read gives none when the session
// cannot carry it: an array, an object that is not a form, or a temporary reference that is not an actor (no other
// game knows it).
[[nodiscard]] std::optional<LocalValue> Read(const BSScript::Variable& acVariable) noexcept;
void Write(BSScript::Variable& aVariable, const LocalValue& acValue) noexcept;
} // namespace GlobalCall
