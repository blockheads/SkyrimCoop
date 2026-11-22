#include <Actor.h>
#include <CriticalStageRules.h>
#include <HookSwitch.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <PlayerCharacter.h>

// Actor::SetCriticalStage (37615): the Papyrus native's and the console's; the ash pile scripts reach it on death.
using TSetCriticalStage = void(Actor*, uint32_t);
static TSetCriticalStage* RealSetCriticalStage = nullptr;

static void HookSetCriticalStage(Actor* apActor, uint32_t aStage)
{
    if (!CriticalStage::Takes(apActor == PlayerCharacter::Get(), aStage))
    {
        spdlog::info("CriticalStage: this game's player keeps its body (stage {} refused)", aStage);
        return;
    }
    RealSetCriticalStage(apActor, aStage);
}

// Papyrus Actor.AttachAshPile (54655): queues the functor that places the pile at the actor.
using TAttachAshPile = void(void*, uint32_t, Actor*, TESForm*);
static TAttachAshPile* RealAttachAshPile = nullptr;

static void HookAttachAshPile(void* apVm, uint32_t aStackId, Actor* apActor, TESForm* apPile)
{
    if (!CriticalStage::TakesAshPile(apActor && apActor == PlayerCharacter::Get()))
    {
        spdlog::info("CriticalStage: this game's player leaves no ash pile");
        return;
    }
    RealAttachAshPile(apVm, aStackId, apActor, apPile);
}

static Hooks::Initializer s_criticalStageHooks([] {
    const HookGate cGate(__FILE__);
    HOOK_ID(37615, &RealSetCriticalStage, HookSetCriticalStage);
    HOOK_ID(54655, &RealAttachAshPile, HookAttachAshPile);
});
