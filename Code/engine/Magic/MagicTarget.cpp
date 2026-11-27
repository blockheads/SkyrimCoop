#include "Magic/MagicTarget.h"
#include <HookSwitch.h>
#include <magic/CastingSafety.h>

#include <Actor.h>
#include <ActorExtension.h>
#include <core/World.h>
#include <ActorExtension.h>
#include "Magic/EffectItem.h"


#include <combat/AddTargetEvent.h>
#include <combat/HitEvent.h>
#include <magic/SummonEvent.h>
#include <GuestNpcs.h>

#include <items/Overrides.h>
#include <PlayerCharacter.h>

#include <Effects/ActiveEffect.h>
#include <Misc/JudgedFor.h>
#include <TESObjectREFR.h>
#include <Papyrus/GuestScripts.h>
#include <Combat/HitOwnership.h>
#include <magic/EffectRules.h>
#include <magic/OwnerEffects.h>
#include <Projectiles/Projectile.h>
#include <Utils.h>
#include <Forms/EffectSetting.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <RTTI.h>
#include <VersionDb.h>

using TAddTarget = bool(MagicTarget*, MagicTarget::AddTargetData& arData);
using TFindTargets = bool(MagicCaster*, float afEffectivenessMult, int32_t* aruiTargetCount, TESBoundObject* apSource, char abLoadCast, char abAdjust);
using TAdjustForPerks = void(ActiveEffect*, Actor* apCaster, MagicTarget* apTarget);
using THasPerk = bool(Actor*, TESForm* apPerk, void* apUnk1, double* afReturnValue);
using TEvaluateEntryConditions = void(ActiveEffect*, float aDelta, bool aForce);

static TAddTarget* RealAddTarget = nullptr;
static TFindTargets* RealFindTargets = nullptr;
static TAdjustForPerks* RealAdjustForPerks = nullptr;
static THasPerk* RealHasPerk = nullptr;
static TEvaluateEntryConditions* RealEvaluateEntryConditions = nullptr;

namespace
{
// Another player's character as a spell's caster: its game's player, for that spell's conditions (conditions.md,
// "A spell from another player's character").
TESObjectREFR* PlayerCaster(TESObjectREFR* apCaster) noexcept
{
    auto* pActor = Cast<Actor>(apCaster);
    return pActor && pActor->GetExtension()->IsOtherPlayer() ? pActor : nullptr;
}
} // namespace

static thread_local bool s_applyHealPerkBonus = false;
static thread_local bool s_applyStaminaPerkBonus = false;

bool MagicTarget::AddTarget(AddTargetData& arData, bool aApplyHealPerkBonus, bool aApplyStaminaPerkBonus) noexcept
{
    s_applyHealPerkBonus = aApplyHealPerkBonus;
    s_applyStaminaPerkBonus = aApplyStaminaPerkBonus;

    // The add check's conditions (34525) judge another player's character as the player who cast it.
    std::optional<JudgedFor::Scope> judged;
    if (TESObjectREFR* pCaster = PlayerCaster(arData.pCaster))
        judged.emplace(pCaster, JudgedFor::Mode::kCaster);
    bool result = RealAddTarget(this, arData);
    judged.reset();

    s_applyHealPerkBonus = false;
    s_applyStaminaPerkBonus = false;

    return result;
}

bool MagicTarget::AddTargetData::ShouldSync()
{
    return !pEffectItem->IsSummonEffect() && !pSpell->IsInvisibilitySpell() && !pSpell->IsWardSpell();
}

// Some effects are player specific, and do not need to be synced.
bool MagicTarget::AddTargetData::IsForbiddenEffect(Actor* apTarget)
{
    if (apTarget != PlayerCharacter::Get())
        return false;

    return pEffectItem->IsNightVisionEffect();
}

Actor* MagicTarget::GetTargetAsActor()
{
    using TGetTargetAsActor = Actor*(MagicTarget*);
    POINTER_SKYRIMSE(TGetTargetAsActor, getTargetAsActor, 34529);
    return getTargetAsActor(this);
}

// MagicTarget::AddTarget (34526): reads the landing, EffectRules::Of decides (magic/EffectRules.h).
bool HookAddTarget(MagicTarget* apThis, MagicTarget::AddTargetData& arData)
{
    Actor* pTargetActor = apThis->GetTargetAsActor();
    spdlog::trace("HookAddTarget: target {:X}, caster {:X}, spell {:X}", pTargetActor ? pTargetActor->formID : 0,
                  arData.pCaster ? arData.pCaster->formID : 0, arData.pSpell ? arData.pSpell->formID : 0);
    if (!pTargetActor)
        return RealAddTarget(apThis, arData);

    Actor* pCasterActor = Cast<Actor>(arData.pCaster);
    const bool cForbidden = arData.IsForbiddenEffect(pTargetActor);

    // The effect precedes the graph switch: derive the descriptor from the graph actually installed.
    if (!cForbidden && (arData.pEffectItem->IsWerewolfEffect() || arData.pEffectItem->IsVampireLordEffect()))
        pTargetActor->GetExtension()->GraphDescriptorHash = 0;

    // A spell's own facts are read only where the rule uses them.
    const bool cPassed = cForbidden || ScopedSpellCastOverride::IsOverriden();
    const auto cVerdict = EffectRules::Of({
        .Target = HitRules::PartyOf(pTargetActor),
        .Caster = HitRules::PartyOf(pCasterActor),
        .TargetIsThisPlayer = pTargetActor == PlayerCharacter::Get(),
        .CasterIsTarget = pCasterActor == pTargetActor,
        .Guest = GuestNpcs::Session() != 0,
        .Hosting = World::Get().IsHosting(),
        .Forbidden = cForbidden,
        .Overridden = cPassed && !cForbidden,
        .Summon = !cPassed && arData.pEffectItem->IsSummonEffect(),
        .Shared = !cPassed && arData.ShouldSync(),
        .Healing = !cPassed && arData.pSpell->IsHealingSpell(),
        .Buff = !cPassed && arData.pSpell->IsBuffSpell(),
        .WorldImpact = Projectile::WorldImpact(),
        .ScriptFor = GuestScripts::NativeGuest(),
    });

    if (cVerdict.AskOwner)
    {
        AddTargetEvent addTargetEvent{};
        addTargetEvent.TargetID = pTargetActor->formID;
        addTargetEvent.CasterID = pCasterActor ? pCasterActor->formID : 0;
        addTargetEvent.SpellID = arData.pSpell->formID;
        addTargetEvent.EffectID = arData.pEffectItem->pEffectSetting->formID;
        addTargetEvent.Magnitude = arData.fMagnitude;
        addTargetEvent.IsDualCasting = arData.bDualCast;
        if (cVerdict.HealPerks)
        {
            addTargetEvent.ApplyHealPerkBonus = pCasterActor->HasPerk(0x581f8);
            addTargetEvent.ApplyStaminaPerkBonus = pCasterActor->HasPerk(0x581f9);
        }
        World::Runner().Trigger(addTargetEvent);
    }
    if (cVerdict.Hit)
        World::Runner().Trigger(HitEvent(pCasterActor->formID, pTargetActor->formID));
    if (!cVerdict.Apply)
        return false;

    if (!cVerdict.Summon)
        return RealAddTarget(apThis, arData);
    // Read before the engine's call, which may change arData.
    const SummonEvent cSummon{arData.pSpell->formID, arData.pEffectItem->pEffectSetting->formID, arData.fMagnitude,
                              {arData.ExplosionLocation.x, arData.ExplosionLocation.y, arData.ExplosionLocation.z}, arData.bDualCast};
    const bool cTaken = RealAddTarget(apThis, arData);
    if (cTaken)
        World::Runner().Trigger(cSummon);
    return cTaken;
}

// MagicTarget::HasMagicEffect (34517) / HasMagicEffectWithKeyword (34518) / MagicItem::IsSpellTarget (34513): a copy
// answers with its owner's effects, to scripts, conditions, hazards and plugins alike (copy-contract.md).
using THasMagicEffect = bool(MagicTarget*, EffectSetting*);
static THasMagicEffect* RealHasMagicEffect = nullptr;
using THasMagicEffectWithKeyword = bool(MagicTarget*, BGSKeyword*, MagicItem**);
static THasMagicEffectWithKeyword* RealHasMagicEffectWithKeyword = nullptr;
using TIsSpellTarget = bool(MagicTarget*, MagicItem*);
static TIsSpellTarget* RealIsSpellTarget = nullptr;

namespace
{
std::shared_ptr<const std::vector<OwnerEffects::Held>> CopyEffectsOf(MagicTarget* apTarget) noexcept
{
    Actor* pActor = apTarget ? apTarget->GetTargetAsActor() : nullptr;
    const auto* pExtension = pActor ? CastingSafety::ExtensionOf(pActor) : nullptr;
    return pExtension && pExtension->IsCopy() ? pExtension->OwnerEffects.load() : nullptr;
}
} // namespace

bool HookHasMagicEffect(MagicTarget* apThis, EffectSetting* apEffect)
{
    if (const auto cHeld = CopyEffectsOf(apThis))
        return apEffect && OwnerEffects::Has(*cHeld, apEffect->formID);
    return RealHasMagicEffect(apThis, apEffect);
}

// A hazard casts only on an actor not under its spell (43987).
bool HookIsSpellTarget(MagicTarget* apThis, MagicItem* apSpell)
{
    if (const auto cHeld = CopyEffectsOf(apThis))
        return apSpell && OwnerEffects::HasSpell(*cHeld, apSpell->formID);
    return RealIsSpellTarget(apThis, apSpell);
}

bool HookHasMagicEffectWithKeyword(MagicTarget* apThis, BGSKeyword* apKeyword, MagicItem** appSpell)
{
    if (const auto cHeld = CopyEffectsOf(apThis))
    {
        const OwnerEffects::Held* pFound = apKeyword ? OwnerEffects::WithKeyword(*cHeld, [apKeyword](uint32_t aId) {
            auto* pEffect = Cast<EffectSetting>(TESForm::GetById(aId));
            return pEffect && pEffect->keywordForm.count > 0 && EngineVirtual(pEffect->keywordForm).Contains(apKeyword);
        }) : nullptr;
        if (appSpell)
            *appSpell = pFound ? Cast<MagicItem>(TESForm::GetById(pFound->Spell)) : nullptr;
        return pFound != nullptr;
    }
    return RealHasMagicEffectWithKeyword(apThis, apKeyword, appSpell);
}

bool HookFindTargets(MagicCaster* apThis, float afEffectivenessMult, int32_t* aruiTargetCount, TESBoundObject* apSource, char abLoadCast, char abAdjust)
{
    return RealFindTargets(apThis, afEffectivenessMult, aruiTargetCount, apSource, abLoadCast, abAdjust);
}

void HookAdjustForPerks(ActiveEffect* apThis, Actor* apCaster, MagicTarget* apTarget)
{
    RealAdjustForPerks(apThis, apCaster, apTarget);

    if (s_applyHealPerkBonus)
        apThis->fMagnitude *= 1.5f;
}

bool HookHasPerk(Actor* apThis, TESForm* apPerk, void* apUnk1, double* afReturnValue)
{
    const auto* pExtension = CastingSafety::ExtensionOf(apThis);
    if (pExtension && pExtension->IsOtherPlayer())
    {
        if (apPerk && apPerk->formID == 0x581f9)
        {
            if (s_applyStaminaPerkBonus)
            {
                if (afReturnValue)
                    *afReturnValue = 1.f;
                return true;
            }
        }
    }

    return RealHasPerk(apThis, apPerk, apUnk1, afReturnValue);
}

// ActiveEffect::EvaluateEntryConditions (34062): a lasting effect's entry conditions, re-read on its owner's game with
// its caster; another player's character is judged as the player there too.
void HookEvaluateEntryConditions(ActiveEffect* apThis, float aDelta, bool aForce)
{
    constexpr uint32_t cHasConditions = 0x80;
    if (!(apThis->uiFlags & cHasConditions) && !apThis->pDisplacementSpell)
        return RealEvaluateEntryConditions(apThis, aDelta, aForce);
    TESObjectREFR* pCaster = PlayerCaster(TESObjectREFR::GetByHandle(apThis->hCaster));
    if (!pCaster)
        return RealEvaluateEntryConditions(apThis, aDelta, aForce);
    const JudgedFor::Scope cJudged(pCaster, JudgedFor::Mode::kCaster);
    RealEvaluateEntryConditions(apThis, aDelta, aForce);
}

static Hooks::Initializer s_magicTargetHooks([]() {
    const HookGate cGate(__FILE__);
    HOOK_ID(34526, &RealAddTarget, HookAddTarget);
    HOOK_ID(34410, &RealFindTargets, HookFindTargets);
    HOOK_ID(34517, &RealHasMagicEffect, HookHasMagicEffect);
    HOOK_ID(34518, &RealHasMagicEffectWithKeyword, HookHasMagicEffectWithKeyword);
    HOOK_ID(34513, &RealIsSpellTarget, HookIsSpellTarget);
    HOOK_ID(34053, &RealAdjustForPerks, HookAdjustForPerks);
    HOOK_ID(21622, &RealHasPerk, HookHasPerk);
    HOOK_ID(34062, &RealEvaluateEntryConditions, HookEvaluateEntryConditions);
});
