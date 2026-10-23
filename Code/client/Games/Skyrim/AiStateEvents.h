#pragma once

// The engine's package, combat, death and scene events (TESPackageEvent, TESCombatEvent, TESDeathEvent,
// TESSceneEvent), combat target switches and the player's in-combat flag raised as AiStateChangeEvent. On while
// anyone started them (HostEngine, GuestEngine).
namespace AiStateEvents
{
void Start() noexcept;
void Stop() noexcept;
} // namespace AiStateEvents
