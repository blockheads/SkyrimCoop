#pragma once

#include <cstdint>
#include <functional>

struct SummonRequest;

// What the host's rules read and change in its game: a guest's magic made in the host's game. A part of HostGame.
struct HostMagicGame
{
    // A guest's player's summon (its request: spell, effect, magnitude, point, dual cast) put on that player's copy,
    // aCasterId, cast by it: the host's engine makes the summon, commanded by the copy (summons.md).
    std::function<void(uint32_t aCasterId, const SummonRequest& acSummon)> Summon;
};
