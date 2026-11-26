#pragma once

// The engine upkeep CombatService runs each frame on the main thread (engine/Actor.cpp, Combat/PartyAggro.cpp).

// Ends an actor's anger with another game's player once Skyrim has resolved the fight (MarkAngryWithRemotePlayer).
void UpdateRemotePlayerAnger() noexcept;
// Refreshes the actors whose locks the hang watchdog reports.
void RefreshHangLockWatch() noexcept;
