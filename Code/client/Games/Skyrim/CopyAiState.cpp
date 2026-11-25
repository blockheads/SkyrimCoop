#include <HookSwitch.h>

#include <Actor.h>
#include <Forms/TESQuest.h>
#include <Forms/TESPackage.h>
#include <Games/ActorExtension.h>
#include <CopyCombat.h>
#include <Diagnostics/Counters.h>
#include <CopyPackage.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <RTTI.h>
#include <VersionDb.h>


// The AI state a copy answers from its owner's where the engine asks an object, not a field
// (docs/design/copy-contract.md, "AI state"; the fields are written by IActorBridge::SetCopyAiFields).

using TGetCurrentScene = BGSScene*(Actor*);
static TGetCurrentScene* RealGetCurrentScene = nullptr;

// Actor::GetCurrentScene (37247, the Actor override of TESObjectREFR vfunc 0x4A): what IsInScene (21682) and
// Papyrus GetCurrentScene read.
static BGSScene* HookGetCurrentScene(Actor* apThis)
{
    if (apThis->GetExtension()->IsCopy())
    {
        const uint32_t cSceneId = apThis->GetExtension()->OwnerSceneId;
        return cSceneId ? Cast<BGSScene>(TESForm::GetById(cSceneId)) : nullptr;
    }
    return RealGetCurrentScene(apThis);
}

BGSScene* Actor::GetOwnCurrentScene() const noexcept
{
    return RealGetCurrentScene ? RealGetCurrentScene(const_cast<Actor*>(this)) : nullptr;
}

using TGetCurrentPackage = TESPackage*(const Actor*);
static TGetCurrentPackage* RealGetCurrentPackage = nullptr;

// 37399 can return an old ExtraPackage behind the copy's idle placeholder. An owner's explicit none stays none.
static TESPackage* HookGetCurrentPackage(const Actor* apActor)
{
    auto* pPackage = RealGetCurrentPackage(apActor);
    const auto* pExtension = const_cast<Actor*>(apActor)->GetExtension();
    return CopyPackage::Read(pExtension->IsCopy(), pExtension->OwnerPackageId.load(std::memory_order_relaxed),
                             pPackage ? pPackage->formID : 0) ? pPackage : nullptr;
}

using TStartCombat = bool(Actor*, Actor* apTarget, void* apGroup);
static TStartCombat* RealStartCombat = nullptr;

// Actor::StartCombat (38561): an NPC's attack runs it on its victim (AIProcess::StartAssault). It decides (ends
// packages, equips, leaves the scene): never on a copy, whose fight CopyCombatGroups::Hold puts on it.
static bool HookStartCombat(Actor* apActor, Actor* apTarget, void* apGroup)
{
    if (!CopyCombat::MayStart(apActor->GetExtension()->IsCopy(), false))
    {
        COUNT("copy combat refused", 1);
        return false;
    }
    return RealStartCombat(apActor, apTarget, apGroup);
}

static Hooks::Initializer s_copyAiStateHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        HOOK_ID(37247, &RealGetCurrentScene, HookGetCurrentScene);
        HOOK_ID(37399, &RealGetCurrentPackage, HookGetCurrentPackage);
        HOOK_ID(38561, &RealStartCombat, HookStartCombat);
    });
