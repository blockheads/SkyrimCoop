#include "RagdollWatch.h"

#include <HookSwitch.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <Havok/BShkbAnimationGraph.h>
#include <Havok/Ragdoll.h>
#include <PlayerCharacter.h>

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

uint32_t HookAddVisitor(uint8_t* apResult, BShkbAnimationGraph** appGraph)
{
    const auto cCaller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    const BShkbAnimationGraph* pGraph = appGraph ? *appGraph : nullptr;
    if (pGraph && pGraph->holder && pGraph->holder == PlayerCharacter::Get())
    {
        std::scoped_lock _{s_lock};
        if (s_adds.size() < 256)
            s_adds.push_back({GetTickCount64(), cCaller - reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr))});
    }
    return RealAddVisitor(apResult, appGraph);
}

// BShkbAnimationGraph::UpdateRagdollPrePhysics (63562): (graph, ...), from the step listener's pre-physics.
using TPrePhysics = char(BShkbAnimationGraph* apGraph, void* apA, void* apB, void* apC);
TPrePhysics* RealPrePhysics = nullptr;

char HookPrePhysics(BShkbAnimationGraph* apGraph, void* apA, void* apB, void* apC)
{
    if (apGraph && apGraph->holder && apGraph->holder == PlayerCharacter::Get() && apGraph->hkWorldM)
        if (const hkbRagdollDriver* pDriver = apGraph->character.ragdollDriver; pDriver && pDriver->ragdoll && pDriver->ragdoll->rigidBodyCount > 0)
            if (const hkpRigidBody* pFirst = pDriver->ragdoll->rigidBodies[0]; pFirst && !pFirst->world)
            {
                std::scoped_lock _{s_lock};
                if (s_readds.size() < 256)
                    s_readds.push_back({GetTickCount64(), 0});
            }
    return RealPrePhysics(apGraph, apA, apB, apC);
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
std::vector<Add> PrePhysicsReadds(bool aClear) noexcept
{
    std::scoped_lock _{s_lock};
    auto readds = s_readds;
    if (aClear)
        s_readds.clear();
    return readds;
}
} // namespace TestHarness::RagdollWatch
