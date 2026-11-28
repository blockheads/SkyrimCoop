#pragma once

#include <cstdint>

// A destructible took damage in this game (TESObjectREFR::DamageObject 19526), not from a host's row: a guest proposes
// its health (docs/re/systems/destruction.md).
struct DestructionEvent
{
    uint32_t FormId{};
};
