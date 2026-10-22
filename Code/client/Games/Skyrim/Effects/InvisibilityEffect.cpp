#include "InvisibilityEffect.h"
#include <HookSwitch.h>

#include <Actor.h>
#include <Games/ActorExtension.h>
#include <Forms/ActorValueInfo.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <VersionDb.h>

using TFinish = void(InvisibilityEffect*);
static TFinish* RealFinish = nullptr;

// This needs to be done because the actor value does not update in time
void HookFinish(InvisibilityEffect* apThis)
{
    if (apThis && apThis->pTarget)
    {
        if (Actor* pActor = apThis->pTarget->GetTargetAsActor())
        {
            if (pActor->GetExtension()->IsCopy())
                pActor->SetActorValue(ActorValueInfo::kInvisibility, 0.f);
        }
    }

    RealFinish(apThis);
}

static Hooks::Initializer s_invisibilityEffectsHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        POINTER_SKYRIMSE(TFinish, s_finish, 34370);

        RealFinish = s_finish.Get();

        HOOK(&RealFinish, HookFinish);
    });
