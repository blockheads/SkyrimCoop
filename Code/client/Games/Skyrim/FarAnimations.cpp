#include <FarAnimations.h>

#include <ClipHolds.h>
#include <HookSwitch.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <NetImmerse/NiNode.h>
#include <TESObjectREFR.h>
#include <VersionDb.h>

#include <atomic>

namespace
{
ClipHolds<const void*> s_holds; // by the object's 3D (its fade node)
std::atomic<bool> s_any{false};

// BSFadeNode::IsFadedIn (our name, 19384): an object's graph is updated (UpdateAnimationJob 19204) only while it is.
using TIsFadedIn = bool(const void* apFadeNode);
TIsFadedIn* RealIsFadedIn = nullptr;

bool HookIsFadedIn(const void* apFadeNode)
{
    return RealIsFadedIn(apFadeNode) ||
           (s_any.load(std::memory_order_relaxed) && s_holds.Held(apFadeNode, std::chrono::steady_clock::now()));
}

Hooks::Initializer s_farAnimationHooks([] {
    const HookGate cGate(__FILE__);
    HOOK_ID(19384, &RealIsFadedIn, HookIsFadedIn);
});
} // namespace

void FarAnimations::Hold(TESObjectREFR& aObject) noexcept
{
    const void* cp3D = aObject.GetNiNode();
    if (!cp3D)
        return;
    s_holds.Hold(cp3D, std::chrono::steady_clock::now());
    s_any = true;
}
