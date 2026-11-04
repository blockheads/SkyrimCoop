#include <Games/Skyrim/PlayerCollision.h>

#include <Games/Skyrim/CollisionGroups.h>

#include <Actor.h>
#include <AI/AIProcess.h>
#include <Havok/bhkCharacterController.h>
#include <HookSwitch.h>
#include <Misc/MiddleProcess.h>

#include <algorithm>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <RTTI.h>
#include <VersionDb.h>

namespace
{
// The stacked actors' collision groups (an actor's bodies all share one).
CollisionGroups s_stacked;

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
    // Two different stacked actors pass through each other; anything else collides as the engine says.
    const uint32_t cGroupA = aFilterA >> 16;
    const uint32_t cGroupB = aFilterB >> 16;
    if (cGroupA && cGroupB && cGroupA != cGroupB)
    {
        if (s_stacked.Has(cGroupA) && s_stacked.Has(cGroupB))
            return false;
    }
    return RealCompareFilterInfo(apThis, aFilterA, aFilterB);
}

Hooks::Initializer s_playerCollisionHooks([] {
    const HookGate cGate(__FILE__);
    HOOK_ID(78553, &RealCompareFilterInfo, HookCompareFilterInfo);
});
} // namespace

namespace PlayerCollision
{
void SetPassThrough(const std::vector<uint32_t>& acActorFormIds) noexcept
{
    std::vector<uint16_t> groups;
    for (const uint32_t cFormId : acActorFormIds)
        if (const uint16_t cGroup = GroupOf(cFormId); cGroup && std::ranges::find(groups, cGroup) == groups.end())
            groups.push_back(cGroup);
    // One actor alone passes through nobody.
    if (groups.size() < 2)
        groups.clear();
    std::ranges::sort(groups);
    if (s_stacked.Publish(groups))
        spdlog::info("Player collision: {} stacked actors pass through each other", groups.size());
}
} // namespace PlayerCollision
