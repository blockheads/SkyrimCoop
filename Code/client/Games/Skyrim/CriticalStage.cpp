#include <Actor.h>
#include <Camera/PlayerCamera.h>
#include <CriticalStage.h>
#include <CriticalStageRules.h>
#include <HookSwitch.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <MainLoopTasks.h>
#include <NetImmerse/NiAVObject.h>
#include <PlayerCharacter.h>

// Actor::SetCriticalStage (37615): the Papyrus native's and the console's; the ash pile scripts reach it on death.
using TSetCriticalStage = void(Actor*, uint32_t);
static TSetCriticalStage* RealSetCriticalStage = nullptr;

// PlayerCharacter::ShowFirstPersonRoot (40476): the camera's switch of the two roots' kHidden.
using TShowFirstPersonRoot = void(PlayerCharacter*, bool);
static TShowFirstPersonRoot* RealShowFirstPersonRoot = nullptr;

// This game's player died to an ash or goo end: its third-person root kept hidden until it lives again.
static std::atomic_bool s_ashDead{false};

// On the main thread, or where 40476 runs inline: the camera's writes to the same flags.
static void KeepBodyHidden(PlayerCharacter& aPlayer) noexcept
{
    if (!CriticalStage::KeepsBodyHidden(s_ashDead.load(), aPlayer.IsDead()))
    {
        s_ashDead.store(false);
        return;
    }
    if (NiAVObject* pBody = aPlayer.Get3D1(false))
        pBody->flags |= NiAVObject::kHidden;
}

static void HookShowFirstPersonRoot(PlayerCharacter* apPlayer, bool aFirstPerson)
{
    RealShowFirstPersonRoot(apPlayer, aFirstPerson);
    using TShouldUseTaskQueue = bool();
    POINTER_SKYRIMSE(TShouldUseTaskQueue, s_shouldUseTaskQueue, 39033);
    // Off the main thread it was queued: the task calls in again.
    if (apPlayer && !s_shouldUseTaskQueue.Get()())
        KeepBodyHidden(*apPlayer);
}

void CriticalStage::ShowPlayerBody() noexcept
{
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    PlayerCamera* pCamera = PlayerCamera::Get();
    // The roots as the camera has them (40476 queues itself off the main thread).
    if (s_ashDead.exchange(false) && pPlayer && pCamera)
        RealShowFirstPersonRoot(pPlayer, pCamera->IsFirstPerson());
}

static void HookSetCriticalStage(Actor* apActor, uint32_t aStage)
{
    const bool cPlayer = apActor == PlayerCharacter::Get();
    if (!CriticalStage::Takes(cPlayer, aStage))
    {
        spdlog::info("CriticalStage: this game's player keeps its body (stage {} refused)", aStage);
        // Papyrus calls in on a VM thread: hidden from the frame's start.
        if (CriticalStage::HidesBody(cPlayer, apActor->IsDead(), aStage) && !s_ashDead.exchange(true))
            MainLoopTasks::Queue([] {
                if (PlayerCharacter* pPlayer = PlayerCharacter::Get())
                    KeepBodyHidden(*pPlayer);
            });
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
    HOOK_ID(40476, &RealShowFirstPersonRoot, HookShowFirstPersonRoot);
});
