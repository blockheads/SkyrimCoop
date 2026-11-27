#include <StealthEye.h>

#include <Actor.h>
#include <Combat/CombatManager.h>
#include <Crime.h>
#include <ActorExtension.h>
#include <TES.h>
#include <HookSwitch.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <PlayerCharacter.h>
#include <VersionDb.h>

#include <algorithm>
#include <mutex>

namespace
{
std::mutex s_lock;
std::optional<DetectionRules::EyeReading> s_held;

std::optional<DetectionRules::EyeReading> HeldFor(const Actor* apTarget) noexcept
{
    if (!apTarget || apTarget != PlayerCharacter::Get())
        return std::nullopt;
    std::scoped_lock lock(s_lock);
    return s_held;
}

// ProcessLists::RequestHighestDetectionLevelAgainstActor (41408; int32 in eax, 40593 tests all of it, CommonLib says
// int16): the eye, lockpicking, alarms, a stolen item's owner. On a guest its player's answer is the host's NPCs'.
using THighest = int32_t(ProcessLists*, Actor*, uint32_t&);
THighest* RealHighest = nullptr;

int32_t HookHighest(ProcessLists* apThis, Actor* apTarget, uint32_t& aSeeing)
{
    if (const auto cHeld = HeldFor(apTarget))
    {
        aSeeing = cHeld->Seeing;
        return cHeld->Level;
    }
    return RealHighest(apThis, apTarget, aSeeing);
}

// CombatManager's lowest stealth points of an actor over its combat groups (46877): the eye in combat (40593).
using TLowestStealth = float(void*, Actor*);
TLowestStealth* RealLowestStealth = nullptr;

float HookLowestStealth(void* apManager, Actor* apActor)
{
    if (const auto cHeld = HeldFor(apActor))
        return cHeld->Stealth;
    return RealLowestStealth(apManager, apActor);
}

Hooks::Initializer s_stealthEyeHooks([] {
    const HookGate cGate(__FILE__);
    HOOK_ID(41408, &RealHighest, HookHighest);
    HOOK_ID(46877, &RealLowestStealth, HookLowestStealth);
});
} // namespace

namespace StealthEye
{
DetectionRules::EyeReading Read(Actor& aTarget) noexcept
{
    // 41408's walk for aTarget with the exclusions it applies to the real player only (0x1407863FE).
    using TFreshState = CrimeEngine::DetectionState*(Actor*, Actor*, uint32_t);
    POINTER_SKYRIMSE(TFreshState, s_freshState, 37758); // Actor__GetFreshDetectionStateOf, priority 3 as 41408
    DetectionRules::EyeReading reading;
    const auto& cHandles = ProcessLists::Get()->highActorHandleArray;
    for (uint32_t i = 0; i < cHandles.length; ++i)
    {
        auto* pWitness = Cast<Actor>(TESObjectREFR::GetByHandle(cHandles[i]));
        if (!pWitness || pWitness == &aTarget || pWitness->IsDead() || !pWitness->currentProcess || !(pWitness->flags1 & Actor::kProcessMe))
            continue;
        if (!DetectionRules::CountsOnEye((pWitness->flags2 & Actor::kDoNotShowOnStealthMeter) != 0, (pWitness->flags1 & Actor::kPlayerTeammate) != 0,
                                         pWitness->GetCommandingActor() == &aTarget, pWitness->GetExtension()->IsCopy()))
            continue;
        if (const auto* pState = s_freshState.Get()(pWitness, &aTarget, 3))
        {
            reading.Level = std::max(reading.Level, pState->Level);
            reading.Seeing += pState->Seen;
        }
    }
    if (CombatManager* pManager = CombatManager::Get())
        reading.Stealth = RealLowestStealth ? RealLowestStealth(pManager, &aTarget) : pManager->GetLowestStealthPoints(&aTarget);
    return reading;
}

void Hold(const std::optional<DetectionRules::EyeReading>& acReading) noexcept
{
    std::scoped_lock lock(s_lock);
    s_held = acReading;
}
} // namespace StealthEye
