#pragma once

#include <actorstate/IActorState.h>
#include <animation/IActorAnimation.h>
#include <characters/IActorIdentity.h>
#include <combat/IActorCombat.h>
#include <dialogue/IActorSpeech.h>
#include <items/IActorItems.h>
#include <magic/IActorMagic.h>
#include <movement/IActorMovement.h>

/// Every Actor* operation, with no engine type in a signature: only primitives, std types and Structs/.
/// One part per feature; a service that needs one feature's part can take that part alone.
class IActorBridge : public IActorIdentity,
                     public IActorMovement,
                     public IActorState,
                     public IActorItems,
                     public IActorMagic,
                     public IActorCombat,
                     public IActorAnimation,
                     public IActorSpeech
{
public:
    virtual ~IActorBridge() = default;
};
