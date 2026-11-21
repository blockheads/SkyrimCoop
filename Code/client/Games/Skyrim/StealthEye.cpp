#include <Games/Skyrim/StealthEye.h>

#include <Actor.h>
#include <Games/TES.h>
#include <HookSwitch.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <PlayerCharacter.h>
#include <VersionDb.h>

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

// ProcessLists::RequestHighestDetectionLevelAgainstActor (41408): the eye (40593), lockpicking, alarms, a stolen item's
// owner. On a guest its player's answer is the host's NPCs'.
using THighest = int16_t(ProcessLists*, Actor*, uint32_t&);
THighest* RealHighest = nullptr;

int16_t HookHighest(ProcessLists* apThis, Actor* apTarget, uint32_t& aSeeing)
{
    if (const auto cHeld = HeldFor(apTarget))
    {
        aSeeing = cHeld->Seeing;
        return static_cast<int16_t>(cHeld->Level);
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
    POINTER_SKYRIMSE(THighest, s_highest, 41408);
    POINTER_SKYRIMSE(TLowestStealth, s_lowestStealth, 46877);
    POINTER_SKYRIMSE(void*, s_combatManager, 405246); // CombatManager::singleton
    DetectionRules::EyeReading reading;
    reading.Level = (RealHighest ? RealHighest : s_highest.Get())(ProcessLists::Get(), &aTarget, reading.Seeing);
    if (void* pManager = *s_combatManager.Get())
        reading.Stealth = (RealLowestStealth ? RealLowestStealth : s_lowestStealth.Get())(pManager, &aTarget);
    return reading;
}

void Hold(const std::optional<DetectionRules::EyeReading>& acReading) noexcept
{
    std::scoped_lock lock(s_lock);
    s_held = acReading;
}
} // namespace StealthEye
