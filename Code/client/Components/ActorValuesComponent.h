#pragma once

#include <Structs/ActorValues.h>
#include <optional>

// On an actor this game owns: what was last sent about it.
struct ActorValuesComponent
{
    ActorValues CurrentActorValues{};
    bool IsDead = false;
    std::optional<uint64_t> DeathActionTick;
};
