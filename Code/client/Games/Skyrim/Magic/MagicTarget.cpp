#include "MagicTarget.h"
#include <HookSwitch.h>
#include <Games/Magic/CastingSafety.h>

#include <Actor.h>
#include <Games/ActorExtension.h>
#include <World.h>
#include <Games/ActorExtension.h>
#include "EffectItem.h"


#include <Events/AddTargetEvent.h>
#include <Events/HitEvent.h>
#include <Events/SummonEvent.h>
#include <Games/GuestNpcs.h>

#include <Games/Overrides.h>
#include <PlayerCharacter.h>

#include <Effects/ActiveEffect.h>
#include <Misc/JudgedFor.h>
#include <TESObjectREFR.h>
#include <Papyrus/GuestScripts.h>
#include <Combat/HitOwnership.h>
#include <TrapRules.h>
#include <Games/OwnerEffects.h>
#include <Utils.h>
#include <Forms/EffectSetting.h>
#include <Services/HostService.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <RTTI.h>
#include <VersionDb.h>

using TAddTarget = bool(MagicTarget*, MagicTarget::AddTargetData& arData);
using TFindTargets = bool(MagicCaster*, float afEffectivenessMult, int32_t* aruiTargetCount, TESBoundObject* apSource, char abLoadCast, char abAdjust);
using TAdjustForPerks = void(ActiveEffect*, Actor* apCaster, MagicTarget* apTarget);
using THasPerk = bool(Actor*, TESForm* apPerk, void* apUnk1, double* afReturnValue);
using TGetPerkRank = uint8_t(Actor*, TESForm* apPerk);
using TEvaluateEntryConditions = void(ActiveEffect*, float aDelta, bool aForce);

static TAddTarget* RealAddTarget = nullptr;
static TFindTargets* RealFindTargets = nullptr;
static TAdjustForPerks* RealAdjustForPerks = nullptr;
static THasPerk* RealHasPerk = nullptr;
static TGetPerkRank* RealGetPerkRank = nullptr;
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

void MagicTarget::DispelAllSpells(bool aNow) noexcept
{
    using TDispelAllSpells = void(MagicTarget*, bool aNow);
    POINTER_SKYRIMSE(TDispelAllSpells, dispelAllSpells, 34512);
    dispelAllSpells(this, aNow);
}

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

namespace
{
bool IsCopy(Actor* apActor) noexcept
{
    return apActor && apActor->GetExtension()->IsCopy();
}

// Cast by one of our actors (or our player): ours to send to the target's owner.
bool IsOurCast(Actor* apCaster) noexcept
{
    return apCaster && !IsCopy(apCaster);
}

bool IsFriendly(const MagicItem* apSpell) noexcept
{
    return apSpell->IsHealingSpell() || apSpell->IsBuffSpell();
}

// The host's world on a guest's character: no actor casts it (a statue, a trap's spell or projectile, a hazard, an
// explosion), or a script making the copy cast. The host's verdict, sent to the guest's game.
bool IsWorldCast(Actor* apTarget, Actor* apCaster) noexcept
{
    if (!apTarget->GetExtension()->IsOtherPlayer() || !World::Get().GetHostService().IsHosting())
        return false;
    const uint32_t cGuest = GuestScripts::NativeGuest();
    return !apCaster || (IsCopy(apCaster) && cGuest && cGuest != 0x14); // 0x14: the host's own player's script
}
} // namespace

bool HookAddTarget(MagicTarget* apThis, MagicTarget::AddTargetData& arData)
{
    Actor* pTargetActor = apThis->GetTargetAsActor();
    spdlog::trace("HookAddTarget: target {:X}, caster {:X}, spell {:X}", pTargetActor ? pTargetActor->formID : 0,
                  arData.pCaster ? arData.pCaster->formID : 0, arData.pSpell ? arData.pSpell->formID : 0);
    if (!pTargetActor || arData.IsForbiddenEffect(pTargetActor))
        return RealAddTarget(apThis, arData);

    ActorExtension* pTargetActorEx = pTargetActor->GetExtension();

    // The effect precedes the graph switch: derive the descriptor from the graph actually installed.
    if (arData.pEffectItem->IsWerewolfEffect() || arData.pEffectItem->IsVampireLordEffect())
        pTargetActorEx->GraphDescriptorHash = 0;

    if (ScopedSpellCastOverride::IsOverriden())
        return RealAddTarget(apThis, arData);

    Actor* pCasterActor = Cast<Actor>(arData.pCaster);
    // A guest's player's summon its engine took (34526 can refuse it): the host's engine makes it on this player's copy
    // (summons.md); the effect runs here too, CreateSummon making nothing (SummonCreatureEffect.cpp).
    if (GuestNpcs::Session() && arData.pEffectItem->IsSummonEffect() && pTargetActor == PlayerCharacter::Get() && pCasterActor == pTargetActor)
    {
        const SummonEvent cEvent{arData.pSpell->formID, arData.pEffectItem->pEffectSetting->formID, arData.fMagnitude,
                                 {arData.ExplosionLocation.x, arData.ExplosionLocation.y, arData.ExplosionLocation.z}, arData.bDualCast};
        const bool cTaken = RealAddTarget(apThis, arData);
        if (cTaken)
            World::Get().GetRunner().Trigger(cEvent);
        return cTaken;
    }

    AddTargetEvent addTargetEvent{};
    addTargetEvent.TargetID = pTargetActor->formID;
    addTargetEvent.CasterID = pCasterActor ? pCasterActor->formID : 0;
    addTargetEvent.SpellID = arData.pSpell->formID;
    addTargetEvent.EffectID = arData.pEffectItem->pEffectSetting->formID;
    addTargetEvent.Magnitude = arData.fMagnitude;
    addTargetEvent.IsDualCasting = arData.bDualCast;

    // Direct authority: the caster's game decides that a spell hit, and only the target's owner
    // applies it (effects requested by others arrive through MagicTarget::AddTarget, which skips
    // this hook). Copies never apply effects themselves; they show the owner's resulting state.
    if (pTargetActorEx->IsCopy())
    {
        const bool cWorldCast = IsWorldCast(pTargetActor, pCasterActor);

        // Someone else's actor hit by someone else's spell, or by nothing of ours: their games
        // handle it.
        if (!IsOurCast(pCasterActor) && !cWorldCast)
            return false;

        // Friendly fire: a hostile spell on another player goes to its game like any other (friendly-fire.md).
        if (pTargetActorEx->IsOtherPlayer() && arData.pSpell->IsHealingSpell() && pCasterActor)
        {
            addTargetEvent.ApplyHealPerkBonus = pCasterActor->HasPerk(0x581f8);
            addTargetEvent.ApplyStaminaPerkBonus = pCasterActor->HasPerk(0x581f9);
        }

        // Our spell hit their actor: ask the owner to apply it. A hostile one is a hit for the
        // attacker's feedback (CombatService shows the target's health bar).
        if (arData.ShouldSync())
            World::Get().GetRunner().Trigger(addTargetEvent);
        if (pCasterActor && !IsFriendly(arData.pSpell))
            World::Get().GetRunner().Trigger(HitEvent(pCasterActor->formID, pTargetActor->formID));
        return false;
    }

    // On a guest, its own copy of a world cast (no actor casts it): the host's game decides and asks.
    if (!TrapRules::AppliesWorldEffect(GuestNpcs::Session() != 0, HitRules::PartyOf(pTargetActor), pCasterActor == nullptr, GuestScripts::NativeGuest() == 0x14))
        return false;

    // Our actor. A replayed spell from someone else's character also lands here, but its caster's
    // game already sent us the request for this hit: applying it too would double it.
    if (IsCopy(pCasterActor))
    {
        // Send out a HitEvent because TakeDamage is never triggered (combat targeting).
        World::Get().GetRunner().Trigger(HitEvent(pCasterActor->formID, pTargetActor->formID));
        return false;
    }

    return RealAddTarget(apThis, arData);
}

// MagicTarget::HasMagicEffect (34517) / HasMagicEffectWithKeyword (34518) / MagicItem::IsSpellTarget (34513): a copy
// answers with its owner's effects, to scripts, conditions, hazards and plugins alike (copy-contract.md).
using THasMagicEffect = bool(MagicTarget*, EffectSetting*);
static THasMagicEffect* RealHasMagicEffect = nullptr;
using THasMagicEffectWithKeyword = bool(MagicTarget*, BGSKeyword*, ActiveEffect**);
static THasMagicEffectWithKeyword* RealHasMagicEffectWithKeyword = nullptr;
using TIsSpellTarget = bool(MagicTarget*, MagicItem*);
static TIsSpellTarget* RealIsSpellTarget = nullptr;

namespace
{
std::shared_ptr<const std::vector<uint32_t>> CopyEffectsOf(MagicTarget* apTarget) noexcept
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

// A hazard casts only on an actor not under its spell (43987): the owner holding one of the spell's effects.
bool HookIsSpellTarget(MagicTarget* apThis, MagicItem* apSpell)
{
    if (const auto cHeld = CopyEffectsOf(apThis))
    {
        if (!apSpell)
            return false;
        std::vector<uint32_t> effects;
        for (const EffectItem* pItem : apSpell->listOfEffects)
            if (pItem && pItem->pEffectSetting)
                effects.push_back(pItem->pEffectSetting->formID);
        return OwnerEffects::HasAny(*cHeld, effects);
    }
    return RealIsSpellTarget(apThis, apSpell);
}

bool HookHasMagicEffectWithKeyword(MagicTarget* apThis, BGSKeyword* apKeyword, ActiveEffect** appFound)
{
    if (const auto cHeld = CopyEffectsOf(apThis))
    {
        if (appFound)
            *appFound = nullptr; // a copy holds none of its own (every caller passes none: 19646, 54650)
        const bool cHas = apKeyword && OwnerEffects::HasWithKeyword(*cHeld, [apKeyword](uint32_t aId) {
                              auto* pEffect = Cast<EffectSetting>(TESForm::GetById(aId));
                              return pEffect && pEffect->keywordForm.count > 0 && EngineVirtual(pEffect->keywordForm).Contains(apKeyword);
                          });
        return cHas;
    }
    return RealHasMagicEffectWithKeyword(apThis, apKeyword, appFound);
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

uint8_t HookGetPerkRank(Actor* apThis, TESForm* apPerk)
{
    return RealGetPerkRank(apThis, apPerk);
}

static Hooks::Initializer s_magicTargetHooks([]() {
    const HookGate cGate(__FILE__);
    POINTER_SKYRIMSE(TGetPerkRank, getPerkRank, 37698);

    RealGetPerkRank = getPerkRank.Get();

    HOOK_ID(34526, &RealAddTarget, HookAddTarget);
    HOOK_ID(34410, &RealFindTargets, HookFindTargets);
    HOOK_ID(34517, &RealHasMagicEffect, HookHasMagicEffect);
    HOOK_ID(34518, &RealHasMagicEffectWithKeyword, HookHasMagicEffectWithKeyword);
    HOOK_ID(34513, &RealIsSpellTarget, HookIsSpellTarget);
    HOOK_ID(34053, &RealAdjustForPerks, HookAdjustForPerks);
    HOOK_ID(21622, &RealHasPerk, HookHasPerk);
    HOOK_ID(34062, &RealEvaluateEntryConditions, HookEvaluateEntryConditions);
    //HOOK(&RealGetPerkRank, HookGetPerkRank);
});
