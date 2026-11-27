#include <Actor.h>
#include <Combat/HitOwnership.h>
#include <combat/MeleeStaggerEvent.h>
#include <ActorExtension.h>
#include <HookSwitch.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <core/RunnerService.h>
#include <VersionDb.h>
#include <core/World.h>

#include <intrin.h>

namespace
{
using TStagger = void(Actor*, float, Actor*);
TStagger* RealStagger = nullptr;

void HookStagger(Actor* apActor, float aMagnitude, Actor* apHitter)
{
    POINTER_SKYRIMSE(uint8_t, s_processHit, 38586);
    // ProcessHit's selected melee reaction (1.6.1170). Spells and other native callers keep their own path.
    if (World::Connected() && _ReturnAddress() == s_processHit.Get() + 0xA62)
        switch (HitRules::Of(HitRules::PartyOf(apActor), HitRules::PartyOf(apHitter), false))
        {
        case HitRules::Route::AskOwner:
            World::Runner().Trigger(MeleeStaggerEvent{apActor->formID, apHitter->formID, aMagnitude});
            return;
        case HitRules::Route::Drop:
        case HitRules::Route::Mirror:
            return;
        case HitRules::Route::Apply:
            break;
        }
    RealStagger(apActor, aMagnitude, apHitter);
}

Hooks::Initializer s_meleeStaggerHooks([] {
    const HookGate cGate(__FILE__);
    HOOK_ID(37710, &RealStagger, HookStagger);
});
} // namespace
