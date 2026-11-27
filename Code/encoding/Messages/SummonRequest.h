#pragma once

#include "Message.h"

#include <Structs/GameId.h>

#include <array>

// A guest's player cast a summon on itself: the host's game puts that effect on the guest's copy, so the host's engine
// makes the summon (docs/re/systems/summons.md).
struct SummonRequest final : MessageOf<SummonRequest, kSummonRequest>
{
    template <class TArchive> void Fields(TArchive& aArchive) noexcept
    {
        aArchive(SpellId, EffectId, Magnitude, Location, DualCast);
    }
    bool operator==(const SummonRequest&) const noexcept = default;

    GameId SpellId;
    GameId EffectId; // its summon effect (MGEF)
    float Magnitude{};
    std::array<float, 3> Location{}; // AddTargetData's point (+0x30), the placement's input
    bool DualCast{};
};
