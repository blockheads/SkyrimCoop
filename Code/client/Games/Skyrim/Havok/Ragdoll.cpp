#include <Havok/Ragdoll.h>

#include <BSAnimationGraphManager.h>
#include <Forms/TESObjectCELL.h>
#include <TESObjectREFR.h>
#include <Utils.h>

bool RemoveRagdollFromWorld(TESObjectREFR& aActor) noexcept
{
    BSAnimationGraphManager* pManager = nullptr;
    if (!EngineVirtual(aActor.animationGraphHolder).GetBSAnimationGraph(&pManager) || !pManager)
        return false;
    {
        BSScopedLock<BSRecursiveLock> _{pManager->lock};
        for (uint32_t i = 0; i < pManager->animationGraphs.size; ++i)
            if (BShkbAnimationGraph* pGraph = pManager->animationGraphs.Get(i))
                EngineVirtual(*pGraph).RemoveRagdollFromWorld();
    }
    pManager->Release();
    return true;
}

std::optional<bool> GraphsStepPhysics(const TESObjectREFR& acActor) noexcept
{
    BSAnimationGraphManager* pManager = nullptr;
    if (!EngineVirtual(acActor.animationGraphHolder).GetBSAnimationGraph(&pManager) || !pManager)
        return std::nullopt;
    std::optional<bool> step;
    {
        BSScopedLock<BSRecursiveLock> _{pManager->lock};
        for (uint32_t i = 0; i < pManager->animationGraphs.size && !step.value_or(false); ++i)
            if (const BShkbAnimationGraph* pGraph = pManager->animationGraphs.Get(i))
                step = pGraph->hkWorldM != nullptr;
    }
    pManager->Release();
    return step;
}

bool SetGraphsStepPhysics(TESObjectREFR& aActor, bool aStep) noexcept
{
    using TGetWorld = void*(TESObjectCELL*);
    POINTER_SKYRIMSE(TGetWorld, s_getWorld, 18995); // TESObjectCELL::GetbhkWorld
    void* pWorld = aStep && aActor.parentCell ? s_getWorld(aActor.parentCell) : nullptr;
    const auto cStep = GraphsStepPhysics(aActor);
    if (!cStep || *cStep == aStep || (aStep && !pWorld))
        return false;
    BSAnimationGraphManager* pManager = nullptr;
    if (!EngineVirtual(aActor.animationGraphHolder).GetBSAnimationGraph(&pManager) || !pManager)
        return false;
    if (aStep)
    {
        BSScopedLock<BSRecursiveLock> _{pManager->lock}; // the update lock 39912 holds (+0xA0)
        for (uint32_t i = 0; i < pManager->animationGraphs.size; ++i)
            if (BShkbAnimationGraph* pGraph = pManager->animationGraphs.Get(i))
                EngineVirtual(*pGraph).ResetRagdoll();
    }
    pManager->Release();
    // The visitor's argument: the world and a flag it sets once a graph took it.
    struct WorldArgument
    {
        void* pWorld;
        bool Done;
    } argument{pWorld, false};
    using TSetWorld = void(void*, WorldArgument*);
    POINTER_SKYRIMSE(TSetWorld, s_setWorld, 38087); // IAnimationGraphManagerHolder::SetWorld, under the manager's lock
    s_setWorld(&aActor.animationGraphHolder, &argument);
    return true;
}

std::vector<bhkWorldObject*> RagdollBodiesOf(const TESObjectREFR& acActor) noexcept
{
    std::vector<bhkWorldObject*> bodies;
    BSAnimationGraphManager* pManager = nullptr;
    if (!EngineVirtual(acActor.animationGraphHolder).GetBSAnimationGraph(&pManager) || !pManager)
        return bodies;
    {
        BSScopedLock<BSRecursiveLock> _{pManager->lock};
        BShkbAnimationGraph* pGraph = pManager->animationGraphIndex < pManager->animationGraphs.size
                                          ? pManager->animationGraphs.Get(pManager->animationGraphIndex)
                                          : nullptr;
        const hkbRagdollDriver* pDriver = pGraph ? pGraph->character.ragdollDriver : nullptr;
        const hkaRagdollInstance* pRagdoll = pDriver ? pDriver->ragdoll : nullptr;
        if (pRagdoll && pRagdoll->rigidBodies)
            for (int32_t i = 0; i < pRagdoll->rigidBodyCount && i < 255; ++i)
                bodies.push_back(pRagdoll->rigidBodies[i] ? pRagdoll->rigidBodies[i]->userData : nullptr);
    }
    pManager->Release();
    return bodies;
}

