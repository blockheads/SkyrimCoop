#pragma once

// The engine's package, combat, death and scene events (TESPackageEvent, TESCombatEvent, TESDeathEvent,
// TESSceneEvent) raised as AiStateChangeEvent. On while anyone started them (HostEngine, GuestEngine).
namespace AiStateEvents
{
void Start() noexcept;
void Stop() noexcept;
} // namespace AiStateEvents
