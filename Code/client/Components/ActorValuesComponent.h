#pragma once

#ifndef TP_INTERNAL_COMPONENTS_GUARD
#error Include Components.h instead
#endif

#include <Structs/ActorValues.h>

// On an actor this game owns: what was last sent about it.
struct ActorValuesComponent
{
    ActorValues CurrentActorValues{};
    bool IsDead = false;
};
