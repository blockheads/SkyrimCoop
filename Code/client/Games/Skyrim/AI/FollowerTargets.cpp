#include <AI/FollowerTargets.h>

#include <AI/AIProcess.h>
#include <Actor.h>
#include <Forms/TESPackage.h>
#include <Games/ActorExtension.h>
#include <HookSwitch.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <Interface/UI.h>
#include <MainLoopTasks.h>
#include <Misc/ActorValueOwner.h>
#include <Papyrus/GuestScripts.h>
#include <PlayerCharacter.h>
#include <RTTI.h>
#include <SessionIds.h>
#include <TESObjectREFR.h>
#include <VersionDb.h>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <utility>

namespace FollowerTargets
{
namespace
{
// CommonLibSSE-NG PackageTarget; 29846 consumes {actor, owner quest} as its resolution context.
struct PackageTarget
{
    int8_t Type;
    uint8_t Pad01[7];
    union {
        uint32_t Handle;
        void* Object;
    } Target;
    int32_t Value;
    uint32_t Pad14;
};
static_assert(sizeof(PackageTarget) == 0x18);
static_assert(offsetof(PackageTarget, Target) == 8);
struct Context
{
    TESObjectREFR* Actor;
    void* Quest;
};
static_assert(sizeof(Context) == 0x10);

struct Order
{
    uint32_t Reference;
    uint32_t Player;
    Followers::Command What;
};
using Targets = std::map<uint32_t, uint32_t>; // host reference -> member's reference handle
std::atomic<bool> s_active{};
std::atomic<std::shared_ptr<const Targets>> s_targets{std::make_shared<const Targets>()};
std::mutex s_lock;
std::vector<Order> s_orders;

void Record(Actor* apActor, Followers::Command aWhat) noexcept
{
    const uint32_t cPlayer = GuestScripts::NativeGuest();
    if (!s_active || !cPlayer || !apActor)
        return;
    std::scoped_lock lock(s_lock);
    s_orders.push_back({apActor->formID, cPlayer, aWhat});
}

using TTeammate = void(Actor*, bool, bool);
TTeammate* RealTeammate;
void HookTeammate(Actor* apActor, bool aSet, bool aFavors)
{
    RealTeammate(apActor, aSet, aFavors);
    Record(apActor, aSet ? Followers::Command::Recruit : Followers::Command::Dismiss);
}

// 38465 receives the ActorValueOwner subobject (Actor +B8), as CommonLib's vfunc 04 does.
using TSetBase = void(ActorValueOwner*, uint32_t, float);
TSetBase* RealSetBase;
void HookSetBase(ActorValueOwner* apOwner, uint32_t aValue, float aAmount)
{
    RealSetBase(apOwner, aValue, aAmount);
    if (aValue == 95)
        Record(reinterpret_cast<Actor*>(reinterpret_cast<uint8_t*>(apOwner) - offsetof(Actor, actorValueOwner)),
               aAmount == 0.f ? Followers::Command::Follow : Followers::Command::Wait);
}

using TOpenInventory = void(TESObjectREFR*, int32_t);
TOpenInventory* RealOpenInventory;
void HookOpenInventory(TESObjectREFR* apRef, int32_t aMode)
{
    if (s_active && apRef && aMode == 3 && s_targets.load()->contains(apRef->formID))
    {
        const uint32_t cPlayer = GuestScripts::NativeGuest();
        std::scoped_lock lock(s_lock);
        s_orders.push_back({apRef->formID, cPlayer ? cPlayer : 0x14, Followers::Command::Trade});
        return;
    }
    RealOpenInventory(apRef, aMode);
}

using TResolve = uint32_t*(PackageTarget*, uint32_t*, Context*);
TResolve* RealResolve;
uint32_t* HookResolve(PackageTarget* apTarget, uint32_t* apOut, Context* apContext)
{
    auto* pResult = RealResolve(apTarget, apOut, apContext);
    if (!s_active || !apContext || !apContext->Actor || !apTarget || apTarget->Type != 0)
        return pResult;
    const auto cTargets = s_targets.load();
    const auto it = cTargets->find(apContext->Actor->formID);
    if (it == cTargets->end())
        return pResult;
    auto* pActor = static_cast<Actor*>(apContext->Actor); // only alias actors enter the target map
    const auto* pPackage = pActor->GetCurrentPackage();
    if (it->second && pPackage && pPackage->formID == 0x5C84B &&
        TESObjectREFR::GetByHandle(*apOut) == PlayerCharacter::Get())
        *apOut = it->second;
    return pResult;
}

void Reevaluate(const Targets& acBefore, const Targets& acAfter) noexcept
{
    for (const auto& [cReference, cHandle] : acBefore)
        if (!acAfter.contains(cReference))
            if (auto* pActor = Cast<Actor>(TESForm::GetById(cReference)))
            {
                using TEvaluate = void(Actor*, bool, bool);
                POINTER_SKYRIMSE(TEvaluate, s_evaluate, 37401);
                const auto* pPackage = pActor->GetCurrentPackage();
                s_evaluate(pActor, true, cHandle && pPackage && pPackage->formID == 0x5C84B && !pActor->IsInCombat());
            }
    for (const auto& [cReference, cHandle] : acAfter)
        if (!acBefore.contains(cReference) || acBefore.at(cReference) != cHandle)
            if (auto* pActor = Cast<Actor>(TESForm::GetById(cReference)))
            {
                spdlog::info("Followers: {:X} follows handle {:X}", cReference, cHandle);
                using TEvaluate = void(Actor*, bool, bool);
                POINTER_SKYRIMSE(TEvaluate, s_evaluate, 37401);
                const auto* pPackage = pActor->GetCurrentPackage();
                s_evaluate(pActor, true, cHandle && pPackage && pPackage->formID == 0x5C84B && !pActor->IsInCombat());
            }
}

Hooks::Initializer s_hooks([] {
    const HookGate cGate(__FILE__);
    HOOK_ID(37717, &RealTeammate, HookTeammate);
    HOOK_ID(38465, &RealSetBase, HookSetBase);
    HOOK_ID(29846, &RealResolve, HookResolve);
    HOOK_ID(51140, &RealOpenInventory, HookOpenInventory);
});
} // namespace

bool OpenInventory(uint32_t aReference) noexcept
{
    auto* pActor = Cast<Actor>(TESForm::GetById(aReference));
    const auto* pUI = UI::Get();
    if (!pActor || !pUI || pUI->GetMenuOpen("ContainerMenu"))
        return false;
    RealOpenInventory(pActor, 3);
    return true;
}

void Start() noexcept
{
    s_active = true;
}
void Stop() noexcept
{
    s_active = false;
    Set({});
    std::scoped_lock lock(s_lock);
    s_orders.clear();
}

std::vector<Followers::Order> TakeOrders() noexcept
{
    std::vector<Order> orders;
    {
        std::scoped_lock lock(s_lock);
        orders = std::exchange(s_orders, {});
    }
    std::vector<Followers::Order> result;
    for (const auto& cOrder : orders)
        if (const auto cCharacter = Utils::GetSessionIdByFormId(cOrder.Player))
            result.push_back({cOrder.Reference, *cCharacter, cOrder.What});
    return result;
}

void Set(const std::vector<Followers::Target>& acTargets) noexcept
{
    auto targets = std::make_shared<Targets>();
    for (const auto& cTarget : acTargets)
    {
        (*targets)[cTarget.Reference] = 0;
        if (auto* pActor = Cast<Actor>(TESForm::GetById(Utils::GetFormIdBySessionId(cTarget.Character))))
            (*targets)[cTarget.Reference] = pActor->GetHandle().handle.iBits;
    }
    const auto cBefore = s_targets.load();
    if (*cBefore == *targets)
        return;
    s_targets = targets;
    MainLoopTasks::Queue([cBefore, targets] { Reevaluate(*cBefore, *targets); });
}
} // namespace FollowerTargets
