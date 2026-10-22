#include "ActorMagicCaster.h"
#include <HookSwitch.h>

#include <Events/SpellCastEvent.h>
#include <Events/InterruptCastEvent.h>

#include <Games/Skyrim/Actor.h>
#include <Games/ActorExtension.h>

#include <World.h>

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

    // TODO(cosideci): why is this here?
    if (!abSuccess)
        return;

    if (apThis->pCasterActor->GetExtension()->IsCopy())
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

    if (apSpell)
    {
        const uint32_t cCasterFormId = apThis->pCasterActor ? apThis->pCasterActor->formID : 0;
        const bool cHasNiNode = apThis->pCasterActor && apThis->pCasterActor->GetNiNode() != nullptr;
        const uint32_t cCastingSource = static_cast<uint32_t>(apThis->GetCastingSource());
        const bool cIsDualCasting = apThis->GetIsDualCasting();
        World::Get().GetRunner().Trigger(SpellCastEvent(cCasterFormId, apSpell->formID, targetFormId,
                                                       cCastingSource, cIsDualCasting, cHasNiNode));
    }

    RealSpellCast(apThis, abSuccess, auiTargetCount, apSpell);
}

void HookInterruptCast(ActorMagicCaster* apThis, bool abRefund)
{
    ActorExtension* pExtended = apThis->pCasterActor->GetExtension();

    if (pExtended->IsLocal())
        World::Get().GetRunner().Trigger(InterruptCastEvent(apThis->pCasterActor->formID, apThis->eCastingSource));

    RealInterruptCast(apThis, abRefund);
}

// A copy's cast is its owner's, checked and paid for there: free, in the two readers of the flag
// (docs/re/systems/actor-ai-and-copies.md, "A copy's casts").
static void MarkCopyCastFree(ActorMagicCaster* apCaster) noexcept
{
    if (apCaster->pCasterActor && apCaster->pCasterActor->GetExtension()->IsCopy())
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

static TiltedPhoques::Initializer s_actorMagicCasterHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        POINTER_SKYRIMSE(TSpellCast, s_spellCast, 34144);
        POINTER_SKYRIMSE(TInterruptCast, s_interruptCast, 34140);
        POINTER_SKYRIMSE(TCasterUpdate, s_casterUpdate, 34143);
        POINTER_SKYRIMSE(TCheckCast, s_checkCast, 34145);

        RealSpellCast = s_spellCast.Get();
        RealInterruptCast = s_interruptCast.Get();
        RealCasterUpdate = s_casterUpdate.Get();
        RealCheckCast = s_checkCast.Get();

        TP_HOOK(&RealSpellCast, HookSpellCast);
        TP_HOOK(&RealInterruptCast, HookInterruptCast);
        TP_HOOK(&RealCasterUpdate, HookCasterUpdate);
        TP_HOOK(&RealCheckCast, HookCheckCast);
    });
