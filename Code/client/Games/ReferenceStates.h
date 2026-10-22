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
// The change flags a row takes (not the save-data row): what a cell's state is read for.
uint32_t RowFlags() noexcept;
// The row flags the reference's change record holds in this game (what a save keeps), a physics move counted as
// the move row; none for an actor (its owner's).
uint32_t RecordedRows(const TESObjectREFR& acObject) noexcept;
// The same, none for a reference the game created (an id of its own): what a cell's state names.
uint32_t ChangedRows(const TESObjectREFR& acObject) noexcept;
// Makes its change record hold exactly these rows (RecordedRows): the others dropped, a physics move with the move
// row, the missing ones marked.
void RecordRows(TESObjectREFR& aObject, uint32_t aRows) noexcept;
// The reference's state now for aFlags: a change for each row that takes any of them.
void Read(const TESObjectREFR& acObject, const GameId& acId, uint32_t aFlags, const ModSystem& acMods,
          std::vector<ObjectChange>& aChanges) noexcept;
// The enabled row of a reference that is gone (deleted), for one no longer there to read.
ObjectChange Deleted(const GameId& acId) noexcept;
// Whether the reference here already reads as the change (a row's kind only).
bool Matches(const TESObjectREFR& acObject, const ObjectChange& acChange, const ModSystem& acMods) noexcept;
// False for a kind that is an event, not a state, or a change the engine refused.
bool Apply(TESObjectREFR& aObject, const ObjectChange& acChange, const ModSystem& acMods) noexcept;
} // namespace ReferenceStates
