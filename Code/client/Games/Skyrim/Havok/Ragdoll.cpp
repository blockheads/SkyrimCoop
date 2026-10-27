#include <Havok/Ragdoll.h>

#include <BSAnimationGraphManager.h>
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

