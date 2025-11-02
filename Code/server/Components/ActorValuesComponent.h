#pragma once

#ifndef TP_INTERNAL_COMPONENTS_GUARD
#error Include Components.h instead
#endif

#include <Structs/ActorValues.h>

namespace Server
{

struct ActorValuesComponent
{
    ActorValues CurrentActorValues{};
};

} // namespace Server
