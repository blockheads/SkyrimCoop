#pragma once

#include <Structs/ScriptValue.h>

#include <optional>

// Script values between this game and the session, with no engine types (ScriptValues.cpp): a form an actor's session
// id, else its GameId (ModSystem); back, none for a form not in this game (IFormBridge::FormExists).
namespace Utils
{
// A script value of this game as the session carries it (a form: an actor's session id, else its GameId), and back
// (none: its form is not in this game).
ScriptValue ToScriptValue(const LocalValue& acValue) noexcept;
std::optional<LocalValue> ToLocalValue(const ScriptValue& acValue) noexcept;
// A script call the same way, its self and arguments (none: one names a form not in this game).
ScriptCall ToScriptCall(const LocalCall& acCall) noexcept;
std::optional<LocalCall> ToLocalCall(const ScriptCall& acCall) noexcept;
} // namespace Utils
