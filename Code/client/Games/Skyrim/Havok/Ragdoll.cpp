#include <Havok/Ragdoll.h>

#include <BSAnimationGraphManager.h>
#include <TESObjectREFR.h>
#include <Utils.h>

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
