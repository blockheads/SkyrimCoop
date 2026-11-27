#include <Actor.h>
#include <BSCore/LockOrder.h>

#include <characters/ActorRenamedEvent.h>
#include <ExtraData/ExtraTextDisplayData.h>
#include <DisplayNames.h>
#include <HookSwitch.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <VersionDb.h>
#include <core/World.h>

// A quest alias's name on an actor: the host's game tells the others (HostService, docs/re/systems/names.md).
namespace
{
using TAliasOnReference = void(void* apAlias, const uint32_t* apHandle);
TAliasOnReference* RealInitializeRefAlias = nullptr;
TAliasOnReference* RealClearRefAlias = nullptr;

template <TAliasOnReference** tReal>
void AliasOnReference(void* apAlias, const uint32_t* apHandle)
{
    Actor* pActor = apHandle ? Cast<Actor>(TESObjectREFR::GetByHandle(*apHandle)) : nullptr;
    const std::string cBefore = pActor ? pActor->GetDisplayFullName() : "";
    (*tReal)(apAlias, apHandle);
    if (std::string after = pActor ? pActor->GetDisplayFullName() : ""; pActor && cBefore != after)
        World::Runner().Trigger(ActorRenamedEvent{pActor->formID, std::move(after)});
}

using TClearCachedName = void(ExtraTextDisplayData*);
TClearCachedName* RealClearCachedName = nullptr;

// ExtraTextDisplayData::ClearCachedName (12769), from a stores-text alias's fill (25052): a custom name is only its
// cache, so it is kept (a copy's name from its owner: names.md).
void HookClearCachedName(ExtraTextDisplayData* apText)
{
    if (apText && !DisplayNames::CacheClears(apText->pDisplayNameText != nullptr, apText->pOwnerQuest != nullptr, apText->iOwnerInstance))
        return;
    RealClearCachedName(apText);
}

Hooks::Initializer s_hooks([] {
    const HookGate cGate(__FILE__);
    HOOK_ID(24011, &RealInitializeRefAlias, AliasOnReference<&RealInitializeRefAlias>);
    HOOK_ID(24012, &RealClearRefAlias, AliasOnReference<&RealClearRefAlias>);
    HOOK_ID(12769, &RealClearCachedName, HookClearCachedName);
    // Pass-through: the quest and alias locks the call takes are the engine's own order (lock-order.md).
    LockOrder::IgnoreFrames(reinterpret_cast<const void*>(&AliasOnReference<&RealInitializeRefAlias>));
    LockOrder::IgnoreFrames(reinterpret_cast<const void*>(&AliasOnReference<&RealClearRefAlias>));
});
} // namespace
