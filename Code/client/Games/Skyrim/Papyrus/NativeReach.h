#pragma once

#include <Misc/BSScript.h>
#include <Papyrus/CodeReach.h>

// Whether a native acts on this game's player without naming them: its C++ code, or a callee three calls deep,
// addresses one of this game's per-player objects (docs/re/systems/papyrus-guest-scripts.md, "Natives on the
// player"). Read from the code once per native, on first use.
namespace NativeReach
{
[[nodiscard]] bool ReachesThisPlayer(BSScript::IFunction* apFunction) noexcept;

// What a native's own code calls (Call: the controls, the screen, the world; CodeReach.h).
[[nodiscard]] Call CallOf(BSScript::IFunction* apFunction) noexcept;
} // namespace NativeReach
