#pragma once

#include <cstdint>
#include <optional>

// TESDeathEvent (dying and dead), or a successful Resurrect: read on the main thread.
struct ActorDeathChangeEvent
{
    uint32_t FormId;
    bool Final = true;
    bool Resurrected = false;
    std::optional<uint64_t> ActionTick;
};

struct DeathActionReplayedEvent
{
    uint32_t FormId;
    uint64_t Tick;
};
