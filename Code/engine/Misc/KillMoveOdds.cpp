#include <Misc/KillMoveOdds.h>

#include <Actor.h>

#include <HookSwitch.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <tools/DevCommands.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <random>

namespace
{
// 0-100, or 0xFF: the game's own rule.
std::atomic<uint8_t> s_odds{0xFF};

std::optional<uint8_t> Odds() noexcept
{
    const uint8_t cOdds = s_odds.load(std::memory_order_relaxed);
    return cOdds == 0xFF ? std::nullopt : std::optional(cOdds);
}

uint32_t Roll(const void* apAttacker, const void* apTarget) noexcept
{
    static std::mutex s_lock;
    static std::mt19937 s_random{std::random_device{}()};
    static DevCommands::KillMoveRolls s_rolls;
    const std::scoped_lock cLock(s_lock);
    const auto cNowMs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
    return s_rolls.Roll(reinterpret_cast<uintptr_t>(apAttacker), reinterpret_cast<uintptr_t>(apTarget), cNowMs,
                        [] { return std::uniform_int_distribution<uint32_t>(0, 99)(s_random); });
}

// Actor::WouldAttackKill 38559 before its hit probe: a living victim (IsDead 37483, false: essential-down counts as
// dead) the attacker may kill (CanBeKilledBy 37229),
// and an attacker in combat when its process asks for it (39996, EngagingInCombat 38563).
bool Killable(void* apSubject, void* apTarget) noexcept
{
    auto* pAttacker = Cast<Actor>(static_cast<TESForm*>(apSubject));
    auto* pVictim = Cast<Actor>(static_cast<TESForm*>(apTarget));
    using TIsDead = bool(Actor*, bool);
    POINTER_SKYRIMSE(TIsDead, s_isDead, 37483);
    if (!pAttacker || !pVictim || s_isDead.Get()(pVictim, false))
        return false;
    using TCanBeKilledBy = bool(Actor*, Actor*);
    using TProcessGate = bool(AIProcess*);
    using TEngagingInCombat = bool(Actor*);
    POINTER_SKYRIMSE(TCanBeKilledBy, s_canBeKilledBy, 37229);
    POINTER_SKYRIMSE(TProcessGate, s_combatGate, 39996);
    POINTER_SKYRIMSE(TEngagingInCombat, s_engagingInCombat, 38563);
    if (!s_canBeKilledBy.Get()(pVictim, pAttacker))
        return false;
    AIProcess* pProcess = pAttacker->currentProcess;
    return !(pProcess && s_combatGate.Get()(pProcess) && !s_engagingInCombat.Get()(pAttacker));
}

// Condition functions: (subject, param1, param2, result); an empty reference param1 is the check's target
// (TESConditionItem::IsTrue 29924). ShouldAttackKill 21744: param1 the target, 1.0 when
// Actor::WouldAttackKill (38559); IsLastHostileActor 21743: 1.0 when no other hostile is near (ProcessLists).
using TCondition = bool(void* apSubject, void* apParam1, void* apParam2, double* apResult);
TCondition* RealShouldAttackKill = nullptr;
TCondition* RealIsLastHostileActor = nullptr;

bool HookShouldAttackKill(void* apSubject, void* apParam1, void* apParam2, double* apResult)
{
    const bool cOk = RealShouldAttackKill(apSubject, apParam1, apParam2, apResult);
    if (const auto cOdds = Odds())
        *apResult = DevCommands::ShouldAttackKill(*apResult != 0.0, Killable(apSubject, apParam1), cOdds,
                                                  Roll(apSubject, apParam1))
                        ? 1.0
                        : 0.0;
    return cOk;
}

bool HookIsLastHostileActor(void* apSubject, void* apParam1, void* apParam2, double* apResult)
{
    const bool cOk = RealIsLastHostileActor(apSubject, apParam1, apParam2, apResult);
    if (const auto cOdds = Odds())
        *apResult = DevCommands::IsLastHostileActor(*apResult != 0.0, cOdds) ? 1.0 : 0.0;
    return cOk;
}

Hooks::Initializer s_killMoveOddsHooks([]() {
    const HookGate cGate(__FILE__);
    HOOK_ID(21744, &RealShouldAttackKill, HookShouldAttackKill);
    HOOK_ID(21743, &RealIsLastHostileActor, HookIsLastHostileActor);
});
} // namespace

void KillMoveOdds::Set(std::optional<uint8_t> aOdds) noexcept
{
    s_odds.store(aOdds.value_or(0xFF), std::memory_order_relaxed);
}
