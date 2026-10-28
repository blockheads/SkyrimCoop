#include <AI/AIProcess.h>
#include <AI/KnockState.h>
#include <Actor.h>
#include <Games/ActorExtension.h>
#include <Games/Overrides.h>
#include <HookSwitch.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>

// AIProcess::UpdateKnockState (39896): the timer/stillness chooses a get-up; graph lead-in and get-up still run.
using TUpdateKnockState = void(AIProcess*, Actor*);
static TUpdateKnockState* RealUpdateKnockState = nullptr;

static void HookUpdateKnockState(AIProcess* apProcess, Actor* apActor)
{
    const auto* pExtension = apActor->GetExtension();
    const uint8_t cState = apActor->actorState.GetKnockState();
    if (pExtension->IsCopy() && !apActor->IsDead() && (cState == 1 || cState == 3))
        return; // Its owner's GetUpBegin is replayed; an independent choice would play it twice.
    RealUpdateKnockState(apProcess, apActor);
}

void PrepareCopyGetUp(Actor& aActor) noexcept
{
    using TBeginGetUp = void(Actor*, bool);
    POINTER_SKYRIMSE(TBeginGetUp, s_beginGetUp, 40150);
    ScopedReferencesOverride engineMoves;
    s_beginGetUp(&aActor, false);
}

static Hooks::Initializer s_knockStateHooks([] {
    const HookGate cGate(__FILE__);
    HOOK_ID(39896, &RealUpdateKnockState, HookUpdateKnockState);
});
