#include <Misc/KillMoveOdds.h>

#include <HookSwitch.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <Services/DevCommands.h>

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

// The idle tree asks each candidate's conditions many times a swing: one roll per attacker and target a second.
uint32_t Roll(const void* apAttacker, const void* apTarget) noexcept
{
    static std::mutex s_lock;
    static std::mt19937 s_random{std::random_device{}()};
    static const void* s_attacker = nullptr;
    static const void* s_target = nullptr;
    static std::chrono::steady_clock::time_point s_at{};
    static uint32_t s_roll = 0;
    const std::scoped_lock cLock(s_lock);
    const auto cNow = std::chrono::steady_clock::now();
    if (apAttacker != s_attacker || apTarget != s_target || cNow - s_at > std::chrono::seconds(1))
    {
        s_attacker = apAttacker;
        s_target = apTarget;
        s_at = cNow;
        s_roll = std::uniform_int_distribution<uint32_t>(0, 99)(s_random);
    }
    return s_roll;
}

// Condition functions: (subject, param1, param2, result). ShouldAttackKill 21744: param1 the target, 1.0 when
// Actor::WouldAttackKill (38559); IsLastHostileActor 21743: 1.0 when no other hostile is near (ProcessLists).
using TCondition = bool(void* apSubject, void* apParam1, void* apParam2, double* apResult);
TCondition* RealShouldAttackKill = nullptr;
TCondition* RealIsLastHostileActor = nullptr;

bool HookShouldAttackKill(void* apSubject, void* apParam1, void* apParam2, double* apResult)
{
    const bool cOk = RealShouldAttackKill(apSubject, apParam1, apParam2, apResult);
    if (const auto cOdds = Odds())
        *apResult = DevCommands::ShouldAttackKill(*apResult != 0.0, cOdds, Roll(apSubject, apParam1)) ? 1.0 : 0.0;
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
