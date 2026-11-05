#include <Actor.h>

#include <Events/ActorRenamedEvent.h>
#include <HookSwitch.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <VersionDb.h>
#include <World.h>

// A quest alias's name on an actor: only the owner's game runs the quest, so its copies are told (docs/re/systems/names.md).
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
    if (pActor && cBefore != pActor->GetDisplayFullName())
        World::Get().GetRunner().Trigger(ActorRenamedEvent{pActor->formID});
}

Hooks::Initializer s_hooks([] {
    const HookGate cGate(__FILE__);
    HOOK_ID(24011, &RealInitializeRefAlias, AliasOnReference<&RealInitializeRefAlias>);
    HOOK_ID(24012, &RealClearRefAlias, AliasOnReference<&RealClearRefAlias>);
});
} // namespace
