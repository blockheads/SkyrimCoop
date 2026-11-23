#include <Actor.h>
#include <CriticalStage.h>
#include <CriticalStageRules.h>
#include <NetImmerse/NiAVObject.h>
#include <World.h>
#include <HookSwitch.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <PlayerCharacter.h>

// Actor::SetCriticalStage (37615): the Papyrus native's and the console's; the ash pile scripts reach it on death.
using TSetCriticalStage = void(Actor*, uint32_t);
static TSetCriticalStage* RealSetCriticalStage = nullptr;

// Set while a refused stage keeps the player's third-person 3D hidden (NiAVObject kHidden), until its revive.
static std::atomic_bool s_bodyHidden{false};

static void HidePlayerBody(bool aHidden) noexcept
{
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    NiAVObject* pBody = pPlayer ? pPlayer->Get3D1(false) : nullptr;
    if (!pBody)
        return;
    pBody->flags = aHidden ? pBody->flags | NiAVObject::kHidden : pBody->flags & ~NiAVObject::kHidden;
}

void CriticalStage::ShowPlayerBody() noexcept
{
    if (s_bodyHidden.exchange(false))
        HidePlayerBody(false);
}

static void HookSetCriticalStage(Actor* apActor, uint32_t aStage)
{
    const bool cPlayer = apActor == PlayerCharacter::Get();
    if (!CriticalStage::Takes(cPlayer, aStage))
    {
        spdlog::info("CriticalStage: this game's player keeps its body (stage {} refused)", aStage);
        // Papyrus calls in on a VM thread: the 3D's flag set from the main loop.
        if (CriticalStage::HidesBody(cPlayer, aStage) && !s_bodyHidden.exchange(true))
            World::Get().GetRunner().Queue([] { HidePlayerBody(s_bodyHidden.load()); });
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
