#pragma once

#include <Games/Magic/MagicSystem.h>

struct ActiveEffect;
struct MagicItem;
struct TESObjectREFR;
struct Actor;
struct BGSSaveFormBuffer;
struct BGSLoadFormBuffer;
struct Projectile;

struct MagicCaster
{
    virtual ~MagicCaster();
    virtual uint64_t CastSpellImmediate(MagicItem* apSpell, bool abLoadCast, TESObjectREFR* apDesiredTarget, float afEffectivenessMult, bool abAdjustOnlyHostileEffectiveness, float afMagnitudeOverride);
    virtual uint64_t FindTouchTarget();
    virtual char RequestCastImpl();
    virtual bool StartChargeImpl();
    virtual char StartReadyImpl();
    virtual char StartCastImpl();
    virtual void FinishCastImpl();
    virtual uint64_t InterruptCastImpl(bool abRefund);
    virtual uint64_t SpellCast(bool abSuccess, uint32_t auiTargetCount, MagicItem* apSpell);
    virtual uint64_t CheckCast(MagicItem* apSpell, bool abDualCast, float* afEffectStrength, MagicSystem::CannotCastReason* apReason, bool abUseBaseMagicka);
    virtual uint64_t GetCasterStatsObject();
    virtual uint64_t GetCasterAsActor();
    virtual uint64_t GetCasterObjectReference(Actor** appCasterActor);
    virtual uint64_t GetMagicNode();
    virtual void ClearMagicNode();
    virtual void SetCurrentSpellImpl(MagicItem* apSpell);
    virtual uint64_t SelectSpellImpl();
    virtual uint64_t DeselectSpellImpl();
    virtual void SetSkipCheckCast();
    virtual void SetCastingTimerForCharge();
    virtual MagicSystem::CastingSource GetCastingSource();
    virtual bool GetIsDualCasting();
    virtual void SetDualCasting(bool abValue);
    virtual void* SaveGame(BGSSaveFormBuffer* apSaveGameBuffer);
    virtual uint64_t LoadGame(BGSLoadFormBuffer* apLoadGameBuffer);
    virtual uint64_t FinishLoadGame(BGSLoadFormBuffer* apLoadGameBuffer);
    virtual void PrepareSound(MagicSystem::SoundID aeID, MagicItem* apSpell);
    virtual uint64_t AdjustActiveEffect(ActiveEffect* apActiveEffect, float afEffectStrength, bool abAdjustOnlyHostileEffectiveness);

    bool CastSpell(MagicItem* apSpell, TESObjectREFR* apDesiredTarget, bool abLoadCast) noexcept;
    void InterruptCast() noexcept;
    void FindTargets(const NiPoint3& arLocation, Projectile* apProjectile, TESObjectREFR* apCollidee, float afPowerScale, float afAreaOverride, bool abAreaOverrideInWorldUnits, bool abApplyAll);

    // CommonLibSSE-NG's names. Seen holding the attack button with Firebolt (2026-10-11): 2 while it
    // charges, 3 once charged, 4 just after the release, then 0.
    enum State : uint32_t
    {
        kNone = 0,
        kUnk01 = 1,
        kUnk02 = 2, // charging (a held button)
        kReady = 3, // charged: a release casts
        kUnk04 = 4, // released, the cast going out
        kCharging = 5,
        kCasting = 6,
        kUnk07 = 7,
        kUnk08 = 8, // interrupt
        kUnk09 = 9, // interrupt or deselect
    };

    GameArray<uint64_t> hSounds; // BSTArray<BSSoundHandle> hSounds;
    BSPointerHandle<TESObjectREFR*> hDesiredTarget;
    MagicItem* pCurrentSpell;
    MagicCaster::State eState;
    float fCastingTimer;
    float fCurrentSpellCost;
    float fMagnitudeOverride;
    float fNextTargetUpdate;
    float fProjectileTimer;
};

SKYRIM_STRUCT_ASSERT(sizeof(MagicCaster) == 0x48);
