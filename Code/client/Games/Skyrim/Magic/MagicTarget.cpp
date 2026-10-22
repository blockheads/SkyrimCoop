#include "MagicTarget.h"
#include <HookSwitch.h>

#include <Actor.h>
#include <Games/ActorExtension.h>
#include <World.h>
#include <Games/ActorExtension.h>
#include "EffectItem.h"

#include <Structs/Skyrim/AnimationGraphDescriptor_WerewolfBehavior.h>
#include <Structs/Skyrim/AnimationGraphDescriptor_VampireLordBehavior.h>

#include <Events/AddTargetEvent.h>
#include <Events/HitEvent.h>

#include <Games/Overrides.h>
#include <PlayerCharacter.h>

#include <Effects/ActiveEffect.h>
#include <Papyrus/GuestScripts.h>
#include <Services/HostService.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <RTTI.h>
#include <VersionDb.h>

using TAddTarget = bool(MagicTarget*, MagicTarget::AddTargetData& arData);
using TCheckAddEffectTargetData = bool(MagicTarget::AddTargetData*, void* arArgs, float afResistance);
using TFindTargets = bool(MagicCaster*, float afEffectivenessMult, int32_t* aruiTargetCount, TESBoundObject* apSource, char abLoadCast, char abAdjust);
using TAdjustForPerks = void(ActiveEffect*, Actor* apCaster, MagicTarget* apTarget);
using THasPerk = bool(Actor*, TESForm* apPerk, void* apUnk1, double* afReturnValue);
using TGetPerkRank = uint8_t(Actor*, TESForm* apPerk);

static TAddTarget* RealAddTarget = nullptr;
static TCheckAddEffectTargetData* RealCheckAddEffectTargetData = nullptr;
static TFindTargets* RealFindTargets = nullptr;
static TAdjustForPerks* RealAdjustForPerks = nullptr;
static THasPerk* RealHasPerk = nullptr;
static TGetPerkRank* RealGetPerkRank = nullptr;

static thread_local bool s_autoSucceedEffectCheck = false;
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
    s_autoSucceedEffectCheck = true;
    s_applyHealPerkBonus = aApplyHealPerkBonus;
    s_applyStaminaPerkBonus = aApplyStaminaPerkBonus;

    bool result = RealAddTarget(this, arData);

    s_autoSucceedEffectCheck = false;
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

// Our player, or another player's copy.
bool IsPlayer(Actor* apActor) noexcept
{
    return apActor && (apActor == PlayerCharacter::Get() || apActor->GetExtension()->IsPlayer());
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

// The host's world on a guest's character: a statue or trap (no actor), or a script making the copy cast.
bool IsWorldCast(Actor* apTarget, Actor* apCaster) noexcept
{
    if (!apTarget->GetExtension()->IsOtherPlayer() || !World::Get().GetHostService().IsHosting())
        return false;
    return !apCaster || (IsCopy(apCaster) && GuestScripts::NativeGuest());
}

// A player's hostile spell on another player while PvP is off.
bool IsBlockedPvp(Actor* apCaster, const MagicItem* apSpell) noexcept
{
    return IsPlayer(apCaster) && !IsFriendly(apSpell) && !World::Get().GetServerSettings().PvpEnabled;
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

    if (arData.pEffectItem->IsWerewolfEffect())
        pTargetActorEx->GraphDescriptorHash = AnimationGraphDescriptor_WerewolfBehavior::m_key;

    if (arData.pEffectItem->IsVampireLordEffect())
        pTargetActorEx->GraphDescriptorHash = AnimationGraphDescriptor_VampireLordBehavior::m_key;

    if (ScopedSpellCastOverride::IsOverriden())
        return RealAddTarget(apThis, arData);

    AddTargetEvent addTargetEvent{};
    addTargetEvent.TargetID = pTargetActor->formID;
    Actor* pCasterActor = Cast<Actor>(arData.pCaster);
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

        if (pTargetActorEx->IsOtherPlayer())
        {
            // PvP is a player's own cast; the world's (a script making the copy cast) is not.
            if (!cWorldCast && IsBlockedPvp(pCasterActor, arData.pSpell))
                return false;

            if (arData.pSpell->IsHealingSpell() && pCasterActor)
            {
                addTargetEvent.ApplyHealPerkBonus = pCasterActor->HasPerk(0x581f8);
                addTargetEvent.ApplyStaminaPerkBonus = pCasterActor->HasPerk(0x581f9);
            }
        }

        // Our spell hit their actor: ask the owner to apply it. A hostile one is a hit for the
        // attacker's feedback (CombatService shows the target's health bar).
        if (arData.ShouldSync())
            World::Get().GetRunner().Trigger(addTargetEvent);
        if (pCasterActor && !IsFriendly(arData.pSpell))
            World::Get().GetRunner().Trigger(HitEvent(pCasterActor->formID, pTargetActor->formID));
        return false;
    }

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

bool HookCheckAddEffectTargetData(MagicTarget::AddTargetData* apThis, void* arArgs, float afResistance)
{
    if (s_autoSucceedEffectCheck)
        return true;

    return RealCheckAddEffectTargetData(apThis, arArgs, afResistance);
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
    if (apThis && apThis->GetExtension()->IsOtherPlayer())
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

uint8_t HookGetPerkRank(Actor* apThis, TESForm* apPerk)
{
    return RealGetPerkRank(apThis, apPerk);
}

static Hooks::Initializer s_magicTargetHooks([]() {
    const HookGate cGate(__FILE__);
    POINTER_SKYRIMSE(TAddTarget, addTarget, 34526);
    POINTER_SKYRIMSE(TCheckAddEffectTargetData, checkAddEffectTargetData, 34525);
    POINTER_SKYRIMSE(TFindTargets, findTargets, 34410);
    POINTER_SKYRIMSE(TAdjustForPerks, adjustForPerks, 34053);
    POINTER_SKYRIMSE(THasPerk, hasPerk, 21622);
    POINTER_SKYRIMSE(TGetPerkRank, getPerkRank, 37698);

    RealAddTarget = addTarget.Get();
    RealCheckAddEffectTargetData = checkAddEffectTargetData.Get();
    RealFindTargets = findTargets.Get();
    RealAdjustForPerks = adjustForPerks.Get();
    RealHasPerk = hasPerk.Get();
    RealGetPerkRank = getPerkRank.Get();

    HOOK(&RealAddTarget, HookAddTarget);
    HOOK(&RealCheckAddEffectTargetData, HookCheckAddEffectTargetData);
    HOOK(&RealFindTargets, HookFindTargets);
    HOOK(&RealAdjustForPerks, HookAdjustForPerks);
    HOOK(&RealHasPerk, HookHasPerk);
    //HOOK(&RealGetPerkRank, HookGetPerkRank);
});
