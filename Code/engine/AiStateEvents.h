#pragma once

// The engine's package, combat, death and scene events (TESPackageEvent, TESCombatEvent, TESDeathEvent,
// TESSceneEvent), combat target switches, headtrack slot changes and the player's in-combat flag raised as
// AiStateChangeEvent; copies get their owner's headtrack slots. On while
// anyone started them (HostEngine, GuestEngine).
namespace AiStateEvents
{
void Start() noexcept;
void Stop() noexcept;
} // namespace AiStateEvents
