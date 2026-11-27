// TESObjectREFR's world hooks: activation, moves and turns, locks, enabling, scripted animations.
#include <BSCore/LockOrder.h>
#include <FreedWornList.h>
#include <Memory.h>
#include <animation/GraphReference.h>
#include <Animation/VariableSync.h>
#include <animation/ActorStateRules.h>
#include <animation/ReplayState.h>
#include <Havok/AnimationGraphState.h>
#include <atomic>
#include <cstring>
#include <SaveLoad.h>
#include <HookSwitch.h>

#include <References.h>
#include <items/Overrides.h>
#include <ItemChanges.h>
#include <HiddenCopies.h>
#include <GuestNpcs.h>
#include <ExtraData/ExtraOwnership.h>

#include <core/World.h>
#include <physics/BodyService.h>
#include <Bridges/PapyrusService.h>
#include <world/ActivateEvent.h>
#include <Papyrus/GuestScripts.h>
#include <items/InventoryChangeEvent.h>
#include <animation/ScriptAnimationEvent.h>
#include <world/LockChangeEvent.h>
#include <ContainerCount.h>
#include <characters/ReferenceJumpEvent.h>

#include <ExtraData/ExtraContainerChanges.h>
#include <items/ContainerTransfers.h>
#include <Interface/ItemMenuWatch.h>
#include <Interface/UI.h>
#include <ExtraData/ExtraDataList.h>
#include <Forms/TESObjectCELL.h>
#include <Forms/TESWorldSpace.h>

using TActivate = bool(TESObjectREFR*, TESObjectREFR* apActivator, uint8_t aUnk1, TESBoundObject* apObjectToGet, int32_t aCount, char aDefaultProcessing);

// TESObjectREFR::MoveTo_Impl (56626: a script's MoveTo, SetPosition and SetAngle), the console's setpos and setangle
// (22053, 22054). Their callers read no result.
using TMoveTo = void(TESObjectREFR*, void* apTargetHandle, TESObjectCELL* apCell, TESWorldSpace* apWorldSpace, const NiPoint3& acPosition, const NiPoint3& acRotation);

using TPlayAnimationAndWait = bool(void*, uint32_t auiStackID, TESObjectREFR* apSelf, BSFixedString* apAnimation, BSFixedString* apEventName);

using TPlayAnimation = bool(void*, uint32_t auiStackID, TESObjectREFR* apSelf, BSFixedString* apEventName);

using TRotate = void(TESObjectREFR*, float aAngle);

using TLockChange = void(TESObjectREFR*);

static TActivate* RealActivate = nullptr;

static TMoveTo* RealMoveTo = nullptr;

using TSetOnAxis = void(TESObjectREFR*, char aAxis, float aValue);
static TSetOnAxis* RealSetPositionOnAxis = nullptr;

static TSetOnAxis* RealSetAngleOnAxis = nullptr;

static TPlayAnimationAndWait* RealPlayAnimationAndWait = nullptr;

static TPlayAnimation* RealPlayAnimation = nullptr;

static TRotate* RealRotateX = nullptr;

static TRotate* RealRotateY = nullptr;

static TRotate* RealRotateZ = nullptr;

static TLockChange* RealLockChange = nullptr;

void TESObjectREFR::SetRotation(float aX, float aY, float aZ) noexcept
{
    RealRotateX(this, aX);
    RealRotateY(this, aY);
    RealRotateZ(this, aZ);
}

void TESObjectREFR::LockChange() noexcept
{
    RealLockChange(this);
}

bool TESObjectREFR::Activate(TESObjectREFR* apActivator, uint8_t aUnk1, TESBoundObject* aObjectToGet, int32_t aCount, char aDefaultProcessing) noexcept
{
    ScopedActivateOverride _;

    return RealActivate(this, apActivator, aUnk1, aObjectToGet, aCount, aDefaultProcessing);
}

static thread_local bool s_cancelAnimationWaitEvent = false;

bool HookPlayAnimationAndWait(void* apThis, uint32_t auiStackID, TESObjectREFR* apSelf, BSFixedString* apAnimation, BSFixedString* apEventName)
{
    spdlog::debug("Animation: {}, EventName: {}", apAnimation->AsAscii(), apEventName->AsAscii());

    // Sent by every game, passed on from the host's only (HostAuthority::OnScriptAnimation).
    if (!s_cancelAnimationWaitEvent && !ScopedHostChangeOverride::IsOverriden() && (apSelf->formID < 0xFF000000))
        World::Runner().Trigger(ScriptAnimationEvent(apSelf->formID, apAnimation->AsAscii(), apEventName->AsAscii()));

    return RealPlayAnimationAndWait(apThis, auiStackID, apSelf, apAnimation, apEventName);
}

static thread_local bool s_cancelAnimationEvent = false;

bool HookPlayAnimation(void* apThis, uint32_t auiStackID, TESObjectREFR* apSelf, BSFixedString* apEventName)
{
    spdlog::debug("EventName: {}", apEventName->AsAscii());

    if (!s_cancelAnimationEvent && !ScopedHostChangeOverride::IsOverriden() && (apSelf->formID < 0xFF000000))
        World::Runner().Trigger(ScriptAnimationEvent(apSelf->formID, std::string{}, apEventName->AsAscii()));

    return RealPlayAnimation(apThis, auiStackID, apSelf, apEventName);
}

bool HookActivate(TESObjectREFR* apThis, TESObjectREFR* apActivator, uint8_t aUnk1, TESBoundObject* apObjectToGet, int32_t aCount, char aDefaultProcessing)
{
    // Another game's player holds it: its holder's alone, this game's player may not take it (Exclusive holds).
    if (apActivator && apActivator == PlayerCharacter::Get())
        if (const auto* pBodies = World::Get().ctx().find<BodyService>(); pBodies && pBodies->HeldElsewhere(apThis->formID))
            return false;
    Actor* pActivator = Cast<Actor>(apActivator);
    const bool cIsDoor = apThis->baseForm->formType == FormType::Door;

    // An actor's activation (not a book's: only reading one removes it from the cell), or a script's on a door
    // (by a reference: only the host's are passed on, HostAuthority::OnActivate).
    if (apActivator && !ScopedHostChangeOverride::IsOverriden() && (pActivator ? apThis->baseForm->formType != FormType::Book : cIsDoor))
    {
        const auto cOpenState = cIsDoor ? apThis->GetOpenState() : TESObjectREFR::kNone;
        ActivateEvent evt;
        evt.ObjectFormId = apThis->formID;
        evt.ActivatorFormId = apActivator->formID;
        evt.ByReference = !pActivator;
        TESObjectCELL* pCell = apThis->GetParentCellEx();
        evt.ObjectParentCellFormId = pCell ? pCell->formID : 0;
        evt.PreActivationOpenStateValue = static_cast<uint8_t>(cOpenState);
        // Picked before the engine's own pick moves it (17420, this game's player only).
        if (apActivator == PlayerCharacter::Get())
            evt.FurnitureEvent = apThis->PickActivatorFurnitureEvent(*pActivator, aDefaultProcessing);
        World::Runner().Trigger(evt);
    }

    // A door or container the player opens with its key unlocks inside (TESObjectDOOR/CONT::Activate).
    if (apActivator && apActivator == PlayerCharacter::Get())
    {
        ScopedPlayerLockOverride _;
        return RealActivate(apThis, apActivator, aUnk1, apObjectToGet, aCount, aDefaultProcessing);
    }
    return RealActivate(apThis, apActivator, aUnk1, apObjectToGet, aCount, aDefaultProcessing);
}

// An actor put somewhere at once jumps there on the other games too (CharacterService, Movement::Teleported).
void RaiseJump(TESObjectREFR* apThis) noexcept
{
    if (apThis && Cast<Actor>(apThis))
        World::Runner().Trigger(ReferenceJumpEvent{apThis->formID});
}

void HookMoveTo(TESObjectREFR* apThis, void* apTargetHandle, TESObjectCELL* apCell, TESWorldSpace* apWorldSpace, const NiPoint3& acPosition, const NiPoint3& acRotation)
{
    RealMoveTo(apThis, apTargetHandle, apCell, apWorldSpace, acPosition, acRotation);
    RaiseJump(apThis);
}

void HookSetPositionOnAxis(TESObjectREFR* apThis, char aAxis, float aValue)
{
    RealSetPositionOnAxis(apThis, aAxis, aValue);
    RaiseJump(apThis);
}

void HookSetAngleOnAxis(TESObjectREFR* apThis, char aAxis, float aValue)
{
    RealSetAngleOnAxis(apThis, aAxis, aValue);
    RaiseJump(apThis);
}

void HookRotateX(TESObjectREFR* apThis, float aAngle)
{
    if (apThis->formType == Actor::Type)
    {
        const auto pActor = static_cast<Actor*>(apThis);
        // We don't allow remotes to move
        if (pActor->GetExtension()->IsCopy())
            return;
    }

    const float cBefore = apThis->rotation.x;
    RealRotateX(apThis, aAngle);
    apThis->RecordMove(apThis->rotation.x != cBefore);
}

void HookRotateY(TESObjectREFR* apThis, float aAngle)
{
    if (apThis->formType == Actor::Type)
    {
        const auto pActor = static_cast<Actor*>(apThis);
        // We don't allow remotes to move
        if (pActor->GetExtension()->IsCopy())
            return;
    }

    const float cBefore = apThis->rotation.y;
    RealRotateY(apThis, aAngle);
    apThis->RecordMove(apThis->rotation.y != cBefore);
}

void HookRotateZ(TESObjectREFR* apThis, float aAngle)
{
    if (apThis->formType == Actor::Type)
    {
        const auto pActor = static_cast<Actor*>(apThis);
        // We don't allow remotes to move, but where we let the engine (a copy lying in its ragdoll: HookTransformDelta;
        // seated on a mount: its REFRSyncController 20318)
        if (pActor->GetExtension()->IsCopy() && !ScopedReferencesOverride::IsOverriden() &&
            !ReplayState::CopyTakesEnginePlace(false, pActor->extraData.Contains(ExtraDataType::Interaction),
                                               pActor->actorState.GetSitSleepState()))
            return;
    }

    const float cBefore = apThis->rotation.z;
    RealRotateZ(apThis, aAngle);
    apThis->RecordMove(apThis->rotation.z != cBefore);
}

void HookLockChange(TESObjectREFR* apThis)
{
    RealLockChange(apThis);
    if (ScopedHostChangeOverride::IsOverriden())
        return;
    const auto* pLock = apThis->GetLock();
    const bool cByPlayer = ScopedPlayerLockOverride::IsOverriden();
    if(pLock)
        World::Runner().Trigger(LockChangeEvent(apThis->formID, pLock->IsLocked(), pLock->lockLevel, cByPlayer));
    else
        World::Runner().Trigger(LockChangeEvent(apThis->formID, false, 0, cByPlayer));
}

// LockpickingMenu::Unlock (51968, `void (LockpickingMenu*)`): the player's lockpick opened it.
using TLockpickUnlock = void(void*);

static TLockpickUnlock* RealLockpickUnlock = nullptr;

void HookLockpickUnlock(void* apThis)
{
    ScopedPlayerLockOverride _;
    RealLockpickUnlock(apThis);
}

// TESObjectREFR::SetAngle (19786, `void (TESObjectREFR*, const NiPoint3&)`): its move reaches the funnel (RecordMove).
using TSetAngle = void(TESObjectREFR*, const NiPoint3&);

static TSetAngle* RealSetAngle = nullptr;

void HookSetAngle(TESObjectREFR* apThis, const NiPoint3& acAngle)
{
    const NiPoint3 cBefore = apThis->rotation;
    RealSetAngle(apThis, acAngle);
    apThis->RecordMove(apThis->rotation != cBefore);
}

// A hidden copy's enables refused (copy-lifecycle.md): Enable 19800, and SetDisabled 14646 for the raw ones (20271).
// By form id only: SetDisabled also runs on forms still loading.
using TEnable = void(TESObjectREFR*, bool aResetInventory);

static TEnable* RealEnable = nullptr;

void HookEnable(TESObjectREFR* apThis, bool aResetInventory)
{
    if (apThis && HiddenCopies::Contains(apThis->formID))
        return;
    RealEnable(apThis, aResetInventory);
}

using TSetDisabled = bool(TESForm*, bool aDisabled);

static TSetDisabled* RealSetDisabled = nullptr;

bool HookSetDisabled(TESForm* apThis, bool aDisabled)
{
    if (!aDisabled && apThis && HiddenCopies::Contains(apThis->formID))
        return true;
    return RealSetDisabled(apThis, aDisabled);
}

static Hooks::Initializer s_objectReferenceWorldHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        HOOK_ID(19512, &RealLockChange, HookLockChange);
        HOOK_ID(19787, &RealRotateX, HookRotateX);
        HOOK_ID(19788, &RealRotateY, HookRotateY);
        HOOK_ID(19789, &RealRotateZ, HookRotateZ);
        HOOK_ID(19796, &RealActivate, HookActivate);
        HOOK_ID(56626, &RealMoveTo, HookMoveTo);
        HOOK_ID(22053, &RealSetPositionOnAxis, HookSetPositionOnAxis);
        HOOK_ID(22054, &RealSetAngleOnAxis, HookSetAngleOnAxis);
        HOOK_ID(56206, &RealPlayAnimationAndWait, HookPlayAnimationAndWait);
        HOOK_ID(56205, &RealPlayAnimation, HookPlayAnimation);
        HOOK_ID(51968, &RealLockpickUnlock, HookLockpickUnlock);
        HOOK_ID(19786, &RealSetAngle, HookSetAngle);
        HOOK_ID(19800, &RealEnable, HookEnable);
        HOOK_ID(14646, &RealSetDisabled, HookSetDisabled);
    });
