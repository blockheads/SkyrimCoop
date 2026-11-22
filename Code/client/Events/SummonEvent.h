#pragma once

#include <array>
#include <cstdint>

// A guest's player cast a summon on itself; its engine did not make it, the host's will (MagicTarget's HookAddTarget,
// docs/re/systems/summons.md).
struct SummonEvent
{
    uint32_t SpellId{};
    uint32_t EffectId{}; // the summon effect (MGEF)
    float Magnitude{};
    std::array<float, 3> Location{};
    bool DualCast{};
};
