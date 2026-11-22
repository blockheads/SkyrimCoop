#include "RagdollWatch.h"

#include <HookSwitch.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <Havok/BShkbAnimationGraph.h>
#include <Havok/Ragdoll.h>
#include <BSAnimationGraphManager.h>
#include <PlayerCharacter.h>
#include <Utils.h>

#include <intrin.h>
#include <mutex>

namespace TestHarness::RagdollWatch
{
namespace
{
std::mutex s_lock;
std::vector<Add> s_adds;
std::vector<Add> s_readds;

// AnimationGraphVisitor_AddRagdollToWorld (32953): (result, graph pointer slot), the graph's vfunc 2 unless one failed.
using TAddVisitor = uint32_t(uint8_t* apResult, BShkbAnimationGraph** appGraph);
TAddVisitor* RealAddVisitor = nullptr;

// The player's graph's ragdoll, its first body in a world: what an add changes.
bool PlayersRagdollInWorld(const BShkbAnimationGraph* apGraph, bool& aInWorld) noexcept
{
    if (!apGraph || !apGraph->holder || apGraph->holder != PlayerCharacter::Get())
        return false;
    const hkbRagdollDriver* pDriver = apGraph->character.ragdollDriver;
    const hkpRigidBody* pFirst = pDriver && pDriver->ragdoll && pDriver->ragdoll->rigidBodyCount > 0 ? pDriver->ragdoll->rigidBodies[0] : nullptr;
    if (!pFirst)
        return false;
    aInWorld = pFirst->world != nullptr;
    return true;
}

// Counted only if the call put the player's ragdoll in the world (it was out before, in after).
void CountIfAdded(std::vector<Add>& aInto, const BShkbAnimationGraph* apGraph, bool aWatched, bool aBefore, uintptr_t aCallerRva) noexcept
{
    bool after = false;
    if (!aWatched || aBefore || !PlayersRagdollInWorld(apGraph, after) || !after)
        return;
    std::scoped_lock _{s_lock};
    if (aInto.size() < 256)
        aInto.push_back({GetTickCount64(), aCallerRva});
}

uint32_t HookAddVisitor(uint8_t* apResult, BShkbAnimationGraph** appGraph)
{
    const auto cCaller = reinterpret_cast<uintptr_t>(_ReturnAddress()) - reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    const BShkbAnimationGraph* pGraph = appGraph ? *appGraph : nullptr;
    bool before = false;
    const bool cWatched = PlayersRagdollInWorld(pGraph, before);
    const uint32_t cResult = RealAddVisitor(apResult, appGraph);
    CountIfAdded(s_adds, pGraph, cWatched, before, cCaller);
    return cResult;
}

// BShkbAnimationGraph::UpdateRagdollPrePhysics (63562): (graph, two floats, scratch), as listener 63551 calls it.
using TPrePhysics = char(BShkbAnimationGraph* apGraph, float aA, float aB, void* apScratch);
TPrePhysics* RealPrePhysics = nullptr;

char HookPrePhysics(BShkbAnimationGraph* apGraph, float aA, float aB, void* apScratch)
{
    bool before = false;
    const bool cWatched = PlayersRagdollInWorld(apGraph, before);
    const char cResult = RealPrePhysics(apGraph, aA, aB, apScratch);
    CountIfAdded(s_readds, apGraph, cWatched, before, 0);
    return cResult;
}

Hooks::Initializer s_ragdollWatchHooks([]() {
    const HookGate cGate(__FILE__);
    HOOK_ID(32953, &RealAddVisitor, HookAddVisitor);
    HOOK_ID(63562, &RealPrePhysics, HookPrePhysics);
});
} // namespace

std::vector<Add> Adds(bool aClear) noexcept
{
    std::scoped_lock _{s_lock};
    auto adds = s_adds;
    if (aClear)
        s_adds.clear();
    return adds;
}
bool TakeOut(bool aQueued) noexcept
{
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    if (!pPlayer)
        return false;
    if (aQueued)
        return QueueRagdollOutOfWorld(*pPlayer);
    BSAnimationGraphManager* pManager = nullptr;
    if (!EngineVirtual(pPlayer->animationGraphHolder).GetBSAnimationGraph(&pManager) || !pManager)
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

std::vector<Add> PrePhysicsReadds(bool aClear) noexcept
{
    std::scoped_lock _{s_lock};
    auto readds = s_readds;
    if (aClear)
        s_readds.clear();
    return readds;
}
} // namespace TestHarness::RagdollWatch
