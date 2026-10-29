#include "ActorMagicCaster.h"
#include <HookSwitch.h>
#include <Games/Magic/CastingSafety.h>

#include <Events/SpellCastEvent.h>
#include <Events/InterruptCastEvent.h>

#include <Games/Skyrim/Actor.h>
#include <Games/ActorExtension.h>

#include <World.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <VersionDb.h>

using TSpellCast = void(ActorMagicCaster*, bool sbSuccess, int32_t auiTargetCount, MagicItem* apSpell);
using TInterruptCast = void(ActorMagicCaster*, bool);
using TCasterUpdate = void(ActorMagicCaster*, float aDelta);
using TCheckCast = bool(ActorMagicCaster*, MagicItem* apSpell, bool abDualCast, float* apEffectStrength, uint32_t* apReason, bool abUseBaseValueForCost);

static TSpellCast* RealSpellCast = nullptr;
static TInterruptCast* RealInterruptCast = nullptr;
static TCasterUpdate* RealCasterUpdate = nullptr;
static TCheckCast* RealCheckCast = nullptr;

void HookSpellCast(ActorMagicCaster* apThis, bool abSuccess, int32_t auiTargetCount, MagicItem* apSpell)
{
    spdlog::debug("HookSpellCast, abSuccess: {}, auiTargetCount: {}, apSpell: {:X}", abSuccess, auiTargetCount, (uint64_t)apSpell);

    // SpellCast (34144) accepts a missing actor or spell and falls back to the current spell.
    auto* pActor = apThis->pCasterActor;
    auto* pExtension = CastingSafety::ExtensionOf(pActor);
    auto* pSpell = CastingSafety::SpellFor(*apThis, apSpell);
    if (!abSuccess || !pActor || !pSpell || !pExtension)
    {
        RealSpellCast(apThis, abSuccess, auiTargetCount, apSpell);
        return;
    }
    if (pExtension->IsCopy())
        return;

    uint32_t targetFormId = 0;

    if (apThis->hDesiredTarget)
    {
        TESObjectREFR* pDesiredTarget = TESObjectREFR::GetByHandle(apThis->hDesiredTarget.handle.iBits);
        if (pDesiredTarget)
        {
            targetFormId = pDesiredTarget->formID;
        }
    }

    World::Get().GetRunner().Trigger(SpellCastEvent(pActor->formID, pSpell->formID, targetFormId,
                                                   static_cast<uint32_t>(apThis->GetCastingSource()),
                                                   apThis->GetIsDualCasting(), pActor->GetNiNode() != nullptr));

    RealSpellCast(apThis, abSuccess, auiTargetCount, apSpell);
}

void HookInterruptCast(ActorMagicCaster* apThis, bool abRefund)
{
    auto* pActor = apThis->pCasterActor;
    ActorExtension* pExtended = CastingSafety::ExtensionOf(pActor);

    if (pExtended && pExtended->IsLocal())
        World::Get().GetRunner().Trigger(InterruptCastEvent(apThis->pCasterActor->formID, apThis->eCastingSource));

    RealInterruptCast(apThis, abRefund);
}

// A copy's cast is its owner's, checked and paid for there: free, in the two readers of the flag
// (docs/re/systems/actor-ai-and-copies.md, "A copy's casts").
static void MarkCopyCastFree(ActorMagicCaster* apCaster) noexcept
{
    const auto* pExtension = CastingSafety::ExtensionOf(apCaster->pCasterActor);
    if (pExtension && pExtension->IsCopy())
        apCaster->uiFlags |= ActorMagicCaster::kSkipCheckCast;
}

// ActorMagicCaster::Update (34143): spends while charging or channelling.
void HookCasterUpdate(ActorMagicCaster* apThis, float aDelta)
{
    MarkCopyCastFree(apThis);
    RealCasterUpdate(apThis, aDelta);
}

bool HookCheckCast(ActorMagicCaster* apThis, MagicItem* apSpell, bool abDualCast, float* apEffectStrength, uint32_t* apReason, bool abUseBaseValueForCost)
{
    MarkCopyCastFree(apThis);
    return RealCheckCast(apThis, apSpell, abDualCast, apEffectStrength, apReason, abUseBaseValueForCost);
}

static Hooks::Initializer s_actorMagicCasterHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        HOOK_ID(34144, &RealSpellCast, HookSpellCast);
        HOOK_ID(34140, &RealInterruptCast, HookInterruptCast);
        HOOK_ID(34143, &RealCasterUpdate, HookCasterUpdate);
        HOOK_ID(34145, &RealCheckCast, HookCheckCast);
    });
