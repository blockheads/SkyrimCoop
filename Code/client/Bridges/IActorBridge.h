#pragma once

#include <cstdint>
#include <string>
#include <vector>

/// All Actor* and related operations abstracted away from SKSE headers.
/// Implementations must NOT leak any SKSE type into the return type or
/// parameter list — only primitives, std::string, and std::vector<T>.
class IActorBridge
{
public:
    virtual ~IActorBridge() = default;

    // --- Existence / identity ---
    virtual bool ActorExists(uint32_t aFormId) const noexcept = 0;
    virtual bool IsActorDead(uint32_t aFormId) const noexcept = 0;
    virtual bool IsPlayerSummon(uint32_t aFormId) const noexcept = 0;
    virtual bool IsTemporaryActor(uint32_t aFormId) const noexcept = 0;

    // --- Remote / player flags (ActorExtension) ---
    virtual bool IsRemotePlayer(uint32_t aFormId) const noexcept = 0;
    virtual void SetActorRemote(uint32_t aFormId, bool aRemote) noexcept = 0;
    virtual void SetActorPlayer(uint32_t aFormId, bool aPlayer) noexcept = 0;

    // --- Ownership / command ---
    virtual uint32_t GetCommandingActor(uint32_t aFormId) const noexcept = 0;

    // --- State ---
    virtual void KillActor(uint32_t aFormId) noexcept = 0;
    virtual void RespawnActor(uint32_t aFormId) noexcept = 0;
    virtual void DeleteActor(uint32_t aFormId) noexcept = 0;

    // --- Position / movement ---
    virtual void MoveActorTo(uint32_t aFormId, uint32_t aCellId, float aX, float aY, float aZ) noexcept = 0;
    virtual void SetActorRotation(uint32_t aFormId, float aX, float aZ) noexcept = 0;
    virtual void GetActorPosition(uint32_t aFormId, float& aX, float& aY, float& aZ) const noexcept = 0;
    virtual void GetActorRotation(uint32_t aFormId, float& aX, float& aY, float& aZ) const noexcept = 0;

    // --- Actor values ---
    virtual float GetActorValue(uint32_t aFormId, int32_t aIndex) const noexcept = 0;
    virtual float GetActorPermanentValue(uint32_t aFormId, int32_t aIndex) const noexcept = 0;
    virtual void SetActorValue(uint32_t aFormId, int32_t aIndex, float aValue) noexcept = 0;
    virtual void ForceActorValue(uint32_t aFormId, int32_t aForceMode, int32_t aIndex, float aValue) noexcept = 0;
    virtual int32_t GetActorValueCount() const noexcept = 0;

    // --- Equipment / weapon ---
    virtual void EquipItem(uint32_t aActorFormId, uint32_t aItemFormId, bool aUnequipSlot) noexcept = 0;
    virtual void UnequipItem(uint32_t aActorFormId, uint32_t aItemFormId) noexcept = 0;
    virtual void DrawWeapon(uint32_t aFormId, bool aDraw) noexcept = 0;
    virtual bool IsWeaponDrawn(uint32_t aFormId) const noexcept = 0;
    virtual uint32_t GetEquippedWeaponFormId(uint32_t aActorFormId, uint32_t aSlot) const noexcept = 0;

    // --- Actor animation / graph ---
    virtual uint64_t GetAnimationGraphDescriptorHash(uint32_t aFormId) const noexcept = 0;
    virtual void SetAnimationGraphDescriptorHash(uint32_t aFormId, uint64_t aHash) noexcept = 0;
    virtual void GenerateMagicCasters(uint32_t aFormId) noexcept = 0;

    // --- Magic ---
    virtual void SetMagicCasterDualCasting(uint32_t aFormId, uint32_t aCastingSource, bool aDual) noexcept = 0;
    virtual void CastSpell(uint32_t aActorFormId, uint32_t aSpellFormId, uint32_t aCastingSource) noexcept = 0;
    virtual void InterruptCast(uint32_t aFormId, uint32_t aCastingSource) noexcept = 0;
    virtual void RemoveSpell(uint32_t aActorFormId, uint32_t aSpellFormId) noexcept = 0;
    virtual void AddMagicTarget(uint32_t aActorFormId, uint32_t aCasterFormId, uint32_t aTargetFormId,
                                 bool aApplyHealPerkBonus, bool aApplyStaminaPerkBonus) noexcept = 0;

    // --- Inventory ---
    virtual bool IsBleedingOut(uint32_t aFormId) const noexcept = 0;
    virtual void PayCrimeGoldToAllFactions(uint32_t aFormId) noexcept = 0;
    virtual uint32_t GetEquippedShoutFormId(uint32_t aFormId) const noexcept = 0;
    virtual uint32_t GetActiveMagicItemFormId(uint32_t aFormId, uint32_t aSlot) const noexcept = 0;

    // --- Combat / target ---
    virtual uint32_t GetCombatTargetFormId(uint32_t aFormId) const noexcept = 0;

    // --- Base form ---
    virtual uint32_t GetActorBaseFormId(uint32_t aFormId) const noexcept = 0;
    virtual std::string GetActorBaseName(uint32_t aFormId) const noexcept = 0;
    virtual uint32_t GetActorRaceFormId(uint32_t aFormId) const noexcept = 0;

    // --- Node / 3D ---
    virtual bool ActorHas3D(uint32_t aFormId) const noexcept = 0;
};
