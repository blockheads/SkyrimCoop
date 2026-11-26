// TESObjectREFR's load hooks: a reference's 3D loading and its end.
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
#include <ExtraData/ExtraCharge.h>
#include <ExtraData/ExtraCount.h>
#include <ExtraData/ExtraEnchantment.h>
#include <ExtraData/ExtraHealth.h>
#include <ExtraData/ExtraPoison.h>
#include <ExtraData/ExtraSoul.h>
#include <ExtraData/ExtraTextDisplayData.h>
#include <ExtraData/ExtraWorn.h>
#include <ExtraData/ExtraWornLeft.h>
#include <Forms/EnchantmentItem.h>
#include <Forms/AlchemyItem.h>
#include <core/ItemIds.h>
#include <EquipManager.h>
#include <DefaultObjectManager.h>
#include <BSAnimationGraphManager.h>
#include <Havok/BShkbAnimationGraph.h>
#include <Havok/hkbBehaviorGraph.h>
#include <Havok/hkbVariableValueSet.h>
#include <Havok/hkbStateMachine.h>
#include <Forms/TESObjectCELL.h>
#include <Forms/TESObjectACTI.h>
#include <ExtraData/ExtraFlags.h>
#include <Forms/BGSAction.h>
#include <Animation/TESActionData.h>
#include <Forms/TESWorldSpace.h>

#include <Structs/AnimationGraphDescriptorManager.h>
#include <Structs/AnimationVariables.h>

// TESObjectREFR::Load3D (19727; Actor::Load3D 37177 calls it first): a guest's own NPC gets none until the host
// spawns its copy, as a disabled reference (GuestNpcs, actor-ai-and-copies.md). Runs on the loader threads too.
using TLoad3D = NiAVObject*(TESObjectREFR*, bool aBackground);

static TLoad3D* RealLoad3D = nullptr;

NiAVObject* HookLoad3D(TESObjectREFR* apThis, bool aBackground)
{
    if (GuestNpcs::Session() && apThis->formType == Actor::Type && static_cast<Actor*>(apThis)->RefuseHeld3D())
        return nullptr;
    return RealLoad3D(apThis, aBackground);
}

// QueuedReference (QueuedActor, QueuedPlayer...): one 3D load job of TESQueuedHandles (QueueReference 13057).
struct QueuedReference
{
    uint8_t pad0[0x2C0];
    uint32_t refHandle; // 2C0; its model (BackgroundLoad3D 13019) at 2C8
};

static_assert(offsetof(QueuedReference, refHandle) == 0x2C0);

// A held NPC's 3D job ended: QueuedReference::End (13023) or Finish (13022). Out of line: the hooks pass through.
__declspec(noinline) static void EndHeldLoad(uint32_t aHandle)
{
    if (GuestNpcs::Session())
        if (auto* pActor = Cast<Actor>(TESObjectREFR::GetByHandle(aHandle)))
            pActor->FinishLoad3D();
}

using TFinishLoad3D = uint64_t(QueuedReference*);

static TFinishLoad3D* RealFinishLoad3D = nullptr;

static TFinishLoad3D* RealEndLoad3D = nullptr;

uint64_t HookFinishLoad3D(QueuedReference* apThis)
{
    const uint32_t cHandle = apThis->refHandle;
    const uint64_t cResult = RealFinishLoad3D(apThis);
    EndHeldLoad(cHandle);
    return cResult;
}

uint64_t HookEndLoad3D(QueuedReference* apThis)
{
    const uint32_t cHandle = apThis->refHandle;
    const uint64_t cResult = RealEndLoad3D(apThis);
    EndHeldLoad(cHandle);
    return cResult;
}

static Hooks::Initializer s_objectReferenceLoadHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        HOOK_ID(19727, &RealLoad3D, HookLoad3D);
        HOOK_ID(13022, &RealFinishLoad3D, HookFinishLoad3D);
        HOOK_ID(13023, &RealEndLoad3D, HookEndLoad3D);
        // Every reference's 3D job ends through them: a lock order inside is the engine's (lock-order.md, the Havok
        // world against a reference's extra-data list).
        LockOrder::IgnoreFrames(reinterpret_cast<const void*>(&HookFinishLoad3D));
        LockOrder::IgnoreFrames(reinterpret_cast<const void*>(&HookEndLoad3D));
    });
