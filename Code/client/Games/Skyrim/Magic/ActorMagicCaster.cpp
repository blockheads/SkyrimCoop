#include "ActorMagicCaster.h"
#include <HookSwitch.h>

#include <Events/SpellCastEvent.h>
#include <Events/InterruptCastEvent.h>

#include <Games/Skyrim/Actor.h>
#include <Games/ActorExtension.h>

#include <World.h>

TP_THIS_FUNCTION(TSpellCast, void, ActorMagicCaster, bool sbSuccess, int32_t auiTargetCount, MagicItem* apSpell);
TP_THIS_FUNCTION(TInterruptCast, void, ActorMagicCaster, bool);

static TSpellCast* RealSpellCast = nullptr;
static TInterruptCast* RealInterruptCast = nullptr;

void TP_MAKE_THISCALL(HookSpellCast, ActorMagicCaster, bool abSuccess, int32_t auiTargetCount, MagicItem* apSpell)
{
    spdlog::debug("HookSpellCast, abSuccess: {}, auiTargetCount: {}, apSpell: {:X}", abSuccess, auiTargetCount, (uint64_t)apSpell);

    // TODO(cosideci): why is this here?
    if (!abSuccess)
        return;

    if (apThis->pCasterActor->GetExtension()->IsRemote())
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

    TiltedPhoques::ThisCall(RealSpellCast, apThis, abSuccess, auiTargetCount, apSpell);
}

void TP_MAKE_THISCALL(HookInterruptCast, ActorMagicCaster, bool abRefund)
{
    ActorExtension* pExtended = apThis->pCasterActor->GetExtension();

    if (pExtended->IsLocal())
        World::Get().GetRunner().Trigger(InterruptCastEvent(apThis->pCasterActor->formID, apThis->eCastingSource));

    TiltedPhoques::ThisCall(RealInterruptCast, apThis, abRefund);
}

static TiltedPhoques::Initializer s_actorMagicCasterHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        POINTER_SKYRIMSE(TSpellCast, s_spellCast, 34144);
        POINTER_SKYRIMSE(TInterruptCast, s_interruptCast, 34140);

        RealSpellCast = s_spellCast.Get();
        RealInterruptCast = s_interruptCast.Get();

        TP_HOOK(&RealSpellCast, HookSpellCast);
        TP_HOOK(&RealInterruptCast, HookInterruptCast);
    });
