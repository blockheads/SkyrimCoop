#include <Games/Skyrim/PlayerCollision.h>

#include <Actor.h>
#include <AI/AIProcess.h>
#include <Havok/bhkCharacterController.h>
#include <HookSwitch.h>
#include <Misc/MiddleProcess.h>

#include <algorithm>
#include <atomic>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <RTTI.h>
#include <VersionDb.h>

namespace
{
// The stacked players' collision groups (an actor's bodies all share one; 0: an empty slot).
struct Groups
{
    uint16_t Ids[4]{};

    bool Has(uint32_t aGroup) const noexcept { return std::find(std::begin(Ids), std::end(Ids), aGroup) != std::end(Ids); }
    bool operator==(const Groups&) const noexcept = default;
};

// Written by the main thread each frame, read by the physics threads: 8 bytes, so read whole without a lock.
std::atomic<Groups> s_stacked;
static_assert(std::atomic<Groups>::is_always_lock_free);

// The actor's collision group: its character controller's (0 without one).
uint16_t GroupOf(uint32_t aFormId) noexcept
{
    const Actor* pActor = Cast<Actor>(TESForm::GetById(aFormId));
    const AIProcess* pProcess = pActor ? pActor->currentProcess : nullptr;
    const MiddleProcess* pMiddle = pProcess ? pProcess->middleProcess : nullptr;
    if (!pMiddle || !pMiddle->charController)
        return 0;
    uint32_t filter = 0;
    pMiddle->charController->GetCollisionFilterInfo(filter);
    return static_cast<uint16_t>(filter >> 16);
}

// bhkCollisionFilter::CompareFilterInfo (78553): whether two bodies' filter infos collide; every Havok
// collision and query asks it (Precision hooks its seven callers).
using TCompareFilterInfo = bool(void*, uint32_t aFilterA, uint32_t aFilterB);
TCompareFilterInfo* RealCompareFilterInfo = nullptr;

bool HookCompareFilterInfo(void* apThis, uint32_t aFilterA, uint32_t aFilterB)
{
    // Two different stacked players pass through each other; anything else collides as the engine says.
    const uint32_t cGroupA = aFilterA >> 16;
    const uint32_t cGroupB = aFilterB >> 16;
    if (cGroupA && cGroupB && cGroupA != cGroupB)
    {
        const Groups cStacked = s_stacked.load(std::memory_order_relaxed);
        if (cStacked.Has(cGroupA) && cStacked.Has(cGroupB))
            return false;
    }
    return RealCompareFilterInfo(apThis, aFilterA, aFilterB);
}

Hooks::Initializer s_playerCollisionHooks([] {
    const HookGate cGate(__FILE__);
    POINTER_SKYRIMSE(TCompareFilterInfo, s_compareFilterInfo, 78553);
    RealCompareFilterInfo = s_compareFilterInfo.Get();
    HOOK(&RealCompareFilterInfo, HookCompareFilterInfo);
});
} // namespace

namespace PlayerCollision
{
void SetPassThrough(const std::vector<uint32_t>& acActorFormIds) noexcept
{
    Groups stacked;
    size_t count = 0;
    for (const uint32_t cFormId : acActorFormIds)
        if (const uint16_t cGroup = GroupOf(cFormId); cGroup && count < std::size(stacked.Ids))
            stacked.Ids[count++] = cGroup;
    // One player alone passes through nobody.
    if (count < 2)
        stacked = {};
    if (s_stacked.exchange(stacked) != stacked)
        spdlog::info("Player collision: {} stacked players pass through each other", count < 2 ? 0 : count);
}
} // namespace PlayerCollision
