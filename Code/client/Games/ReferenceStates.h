#pragma once

#include <Messages/WorldState.h>

struct TESObjectREFR;
struct ModSystem;

// A world reference's change flags as a guest gets them: a row per state, read on the host now and applied on a guest
// with the engine's own call (its side effects and events). The last row would take every other flag as its save
// data (FormState): off while vanilla's states get rows, so a flag with no row shows (logged once, `coop_test.py
// unsynced`). docs/design/world-state-sync.md, "As built".
namespace ReferenceStates
{
// The change flags worth sending: not the engine's bookkeeping, not the location header's.
uint32_t SentFlags() noexcept;
// The reference's state now for aFlags: a change for each row that takes any of them.
void Read(const TESObjectREFR& acObject, const GameId& acId, uint32_t aFlags, const ModSystem& acMods,
          TiltedPhoques::Vector<ObjectChange>& aChanges) noexcept;
// False for a kind that is an event, not a state, or a change the engine refused.
bool Apply(TESObjectREFR& aObject, const ObjectChange& acChange, const ModSystem& acMods) noexcept;
} // namespace ReferenceStates
