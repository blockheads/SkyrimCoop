#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>

// What this game holds right now, as JSON: a bug report's snapshot.json, and the parts of the test harness's
// status (coop_test.py) it shares. Main thread.
namespace Diagnostics
{
// Everything a bug report keeps: the player, the session, the copies, every loaded actor, menus, the frame.
nlohmann::json Report();

// Everything this game knows about one actor (coop_test.py desync pairs an owner's with its copy's).
nlohmann::json ActorSnapshot(uint32_t aFormId);
// Every loaded actor's snapshot, keyed by form id, with "role" ("own" or "copy") and "player".
nlohmann::json WorldSnapshot();
// The loaded actors this game simulates itself, with health and position.
nlohmann::json LocalActors();
// The copies this game shows, where the mod puts each ("requested") and where the engine has it ("actual").
nlohmann::json Copies();
// The session each game holds: clock, weather, players, party panel, chat, join, HUD, death.
nlohmann::json GameStatus();
// A guest's line to the host over the last second.
nlohmann::json LineStatus();
// The Steam account and session.
nlohmann::json SteamStatus();
// Frames, stalls, the longest frame and update.
nlohmann::json FrameStatus();
// The lock-order checker's edges and reports.
nlohmann::json LockOrderStatus();
// Form ids of the items an actor wears or holds.
nlohmann::json EquippedItems(uint32_t aFormId);
// The spell being cast per casting source: left, right, other, instant.
nlohmann::json CastingSpells(uint32_t aFormId);
} // namespace Diagnostics
