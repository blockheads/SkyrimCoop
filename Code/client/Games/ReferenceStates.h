#pragma once

#include <Messages/WorldState.h>

#include <span>

struct TESObjectREFR;

// A world reference's state the host sends when the change funnel reports its flag: read on the host as one value,
// set on a guest from it with the engine's own call, which runs the change's side effects. One row per state; a flag
// with no row is not sent (a mod's forms: their save data, later). docs/design/world-state-sync.md, "As built".
struct ReferenceState
{
    ObjectChange::Kind Kind;
    uint32_t ChangeFlag; // TESObjectREFR::ChangeFlags
    uint32_t (*Read)(const TESObjectREFR& acObject) noexcept;
    bool (*Apply)(TESObjectREFR& aObject, uint32_t aValue) noexcept;
};

namespace ReferenceStates
{
std::span<const ReferenceState> All() noexcept;
// Every row's change flag.
uint32_t ChangeFlags() noexcept;
// Null for a kind that is an event, not a state.
const ReferenceState* Find(ObjectChange::Kind aKind) noexcept;
} // namespace ReferenceStates
