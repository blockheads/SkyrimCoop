#include "AIProcess.h"

void AIProcess::KnockExplosion(Actor* apActor, const NiPoint3* aSourceLocation, float afMagnitude)
{
    using TKnockExplosion = void(AIProcess*, Actor*, const NiPoint3*, float);
    POINTER_SKYRIMSE(TKnockExplosion, knockExplosion, 39895);
    knockExplosion(this, apActor, aSourceLocation, afMagnitude);
}
