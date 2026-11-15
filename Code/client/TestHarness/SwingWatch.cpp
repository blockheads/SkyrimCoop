#include "SwingWatch.h"

#include <HookSwitch.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <PlayerCharacter.h>
#include <VersionDb.h>

#include <intrin.h>
#include <mutex>

namespace TestHarness::SwingWatch
{
namespace
{
std::mutex s_lock;
Swing s_last;

// 38628's size in 1.7.104 (IDA): a call from inside it is a swing's cast.
constexpr uintptr_t kSwingSize = 0x12E6;

// hkpAllCdPointCollector LinearCastFrom (25931): the collector's sphere swept from origin along direction.
using TLinearCast = bool(void* apCollector, void* apWorld, const float* acpOrigin, const float* acpDirection,
                         float aLength);
TLinearCast* RealLinearCast = nullptr;

bool HookLinearCast(void* apCollector, void* apWorld, const float* acpOrigin, const float* acpDirection, float aLength)
{
    const auto cCaller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    const bool cHit = RealLinearCast(apCollector, apWorld, acpOrigin, acpDirection, aLength);
    VersionDbPtr<uint8_t> swing(38628); // Actor::CalculateCurrentHitTargetForWeaponSwing
    const auto cSwing = reinterpret_cast<uintptr_t>(swing.Get());
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    // The player's: 38628 set the phantom to the swinger's group (25932).
    if (cCaller < cSwing || cCaller >= cSwing + kSwingSize || !pPlayer ||
        GameSwingCast::CollectorGroup(apCollector) != GameSwingCast::GroupOf(*pPlayer))
        return cHit;
    GameSwingCast::Cast cast;
    cast.Cast = true;
    cast.Reach = aLength;
    for (int axis = 0; axis < 3; ++axis)
        cast.Eye[axis] = acpOrigin[axis], cast.Direction[axis] = acpDirection[axis];
    if (cHit)
        cast.Hits = GameSwingCast::HitsOf(apCollector);
    std::scoped_lock _{s_lock};
    ++s_last.Count;
    s_last.Cast = std::move(cast);
    return cHit;
}

Hooks::Initializer s_swingWatchHooks([]() {
    const HookGate cGate(__FILE__);
    HOOK_ID(25931, &RealLinearCast, HookLinearCast);
});
} // namespace

Swing Last() noexcept
{
    std::scoped_lock _{s_lock};
    return s_last;
}
} // namespace TestHarness::SwingWatch
