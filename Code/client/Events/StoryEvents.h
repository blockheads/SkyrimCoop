#pragma once

#include <cstdint>

// A player's character's location changed in this game (TESActorLocationChangeEvent from
// TESObjectREFR::MoveRefToNewSpace 19799, or 37313): another player's copy only.
struct CopyLocationChangeEvent
{
    uint32_t ActorId{};
    uint32_t OldLocationId{};
    uint32_t LocationId{};
};

// This game's player crafted something: its story event (CRFT), as the engine queued it.
struct PlayerCraftEvent
{
    uint32_t WorkbenchId{};
    uint32_t LocationId{};
    uint32_t CreatedId{};
};
