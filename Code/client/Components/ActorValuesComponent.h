#pragma once

#include <Structs/ActorValues.h>

// On an actor this game owns: what was last sent about it.
struct ActorValuesComponent
{
    ActorValues CurrentActorValues{};
    bool IsDead = false;
};
