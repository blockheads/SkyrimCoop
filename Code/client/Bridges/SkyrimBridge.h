#pragma once

#include <Bridges/IActorBridge.h>
#include <Bridges/IFormBridge.h>
#include <Bridges/IPlayerBridge.h>
#include <Bridges/IWorldBridge.h>

#include <atomic>
#include <string>

/// Real SKSE implementation of all five bridge interfaces.
/// This is the ONLY file (along with SkyrimBridge.cpp) that is allowed to
/// include SKSE headers (Actor.h, PlayerCharacter.h, Sky.h, etc.).
/// Compiled only as part of SkyrimTogetherClient — never into test targets.
class SkyrimBridge
    : public IActorBridge
    , public IFormBridge
    , public IPlayerBridge
    , public IWorldBridge
{
public:
    SkyrimBridge() = default;
    ~SkyrimBridge() override = default;

    // -----------------------------------------------------------------
    // IActorBridge
    // -----------------------------------------------------------------
    bool ActorExists(uint32_t aFormId) const noexcept override;
    bool IsActorDead(uint32_t aFormId) const noexcept override;
    bool IsPlayerSummon(uint32_t aFormId) const noexcept override;
    bool IsTemporaryActor(uint32_t aFormId) const noexcept override;
    bool IsOtherPlayer(uint32_t aFormId) const noexcept override;
    void SetActorCopy(uint32_t aFormId, bool aCopy) noexcept override;
    void SetActorPersistent(uint32_t aFormId) noexcept override;
    bool IsActorCopy(uint32_t aFormId) const noexcept override;
    void SetActorPlayer(uint32_t aFormId, bool aPlayer) noexcept override;
    uint32_t GetCommandingActor(uint32_t aFormId) const noexcept override;
    void KillActor(uint32_t aFormId) noexcept override;
    bool DamageActorHealth(uint32_t aFormId, uint32_t aAttackerFormId, float aDamage) noexcept override;
    void RespawnActor(uint32_t aFormId) noexcept override;
    void ResurrectActor(uint32_t aFormId) noexcept override;
    void DeleteActor(uint32_t aFormId) noexcept override;
    void MoveActorTo(uint32_t aFormId, uint32_t aCellId, float aX, float aY, float aZ) noexcept override;
    void SetActorRotation(uint32_t aFormId, float aX, float aZ) noexcept override;
    bool ApplyRemoteMovement(uint32_t aFormId, const float aPosition[3],
        const float aRotation[3], float aDirection, const AnimationVariables& aVariables) noexcept override;
    void GetActorPosition(uint32_t aFormId, float& aX, float& aY, float& aZ) const noexcept override;
    void GetActorRotation(uint32_t aFormId, float& aX, float& aY, float& aZ) const noexcept override;
    float GetActorValue(uint32_t aFormId, int32_t aIndex) const noexcept override;
    float GetActorPermanentValue(uint32_t aFormId, int32_t aIndex) const noexcept override;
    void SetActorValue(uint32_t aFormId, int32_t aIndex, float aValue) noexcept override;
    void ForceActorValue(uint32_t aFormId, int32_t aForceMode, int32_t aIndex, float aValue) noexcept override;
    int32_t GetActorValueCount() const noexcept override;
    bool IsWeaponDrawn(uint32_t aFormId) const noexcept override;
    uint32_t GetEquippedWeaponFormId(uint32_t aActorFormId, uint32_t aSlot) const noexcept override;
    uint64_t GetAnimationGraphDescriptorHash(uint32_t aFormId) const noexcept override;
    void SetAnimationGraphDescriptorHash(uint32_t aFormId, uint64_t aHash) noexcept override;
    void GenerateMagicCasters(uint32_t aFormId) noexcept override;
    void SetMagicCasterDualCasting(uint32_t aFormId, uint32_t aCastingSource, bool aDual) noexcept override;
    void CastSpell(uint32_t aActorFormId, uint32_t aSpellFormId, uint32_t aCastingSource,
                   uint32_t aDesiredTargetFormId) noexcept override;
    void InterruptCast(uint32_t aFormId, uint32_t aCastingSource) noexcept override;
    uint32_t GetCastingSpellFormId(uint32_t aFormId, uint32_t aCastingSource) const noexcept override;
    void RemoveSpell(uint32_t aActorFormId, uint32_t aSpellFormId) noexcept override;
    bool IsBleedingOut(uint32_t aFormId) const noexcept override;
    void PayCrimeGoldToAllFactions(uint32_t aFormId) noexcept override;
    uint32_t GetEquippedShoutFormId(uint32_t aFormId) const noexcept override;
    uint32_t GetActiveMagicItemFormId(uint32_t aFormId, uint32_t aSlot) const noexcept override;
    uint32_t GetCombatTargetFormId(uint32_t aFormId) const noexcept override;
    uint32_t GetActorBaseFormId(uint32_t aFormId) const noexcept override;
    bool SetActorBaseFormId(uint32_t aFormId, uint32_t aBaseFormId) noexcept override;
    std::string GetActorBaseName(uint32_t aFormId) const noexcept override;
    float GetActorBaseWeight(uint32_t aFormId) const noexcept override;
    uint32_t GetActorRaceFormId(uint32_t aFormId) const noexcept override;
    bool ActorHas3D(uint32_t aFormId) const noexcept override;
    bool IsAnimationGraphReady(uint32_t aFormId) const noexcept override;
    std::vector<ActiveEffectInfo> GetActorActiveEffects(uint32_t aFormId) const noexcept override;
    void ReactToAttack(uint32_t aVictimFormId, uint32_t aAttackerFormId) noexcept override;
    uint32_t GetKillerFormId(uint32_t aFormId) const noexcept override;
    std::vector<uint32_t> GetLoadedActorFormIds() const noexcept override;
    ActorValues GetActorEssentialValues(uint32_t aFormId) const noexcept override;
    void SetActorValues(uint32_t aFormId, const ActorValues& acValues) noexcept override;
    Inventory GetActorInventory(uint32_t aFormId) const noexcept override;
    void SetActorInventory(uint32_t aFormId, const Inventory& acInventory) noexcept override;
    Factions GetActorFactions(uint32_t aFormId) const noexcept override;
    void SetActorFactions(uint32_t aFormId, const Factions& acFactions) noexcept override;
    void SetActorWeaponDrawnEx(uint32_t aFormId, bool aDraw) noexcept override;
    WeaponState GetWeaponState(uint32_t aFormId) const noexcept override;
    bool IsActorTemporary(uint32_t aFormId) const noexcept override;
    bool IsActorMount(uint32_t aFormId) const noexcept override;
    bool IsActorDragon(uint32_t aFormId) const noexcept override;
    bool IsActorVampireLord(uint32_t aFormId) const noexcept override;
    bool IsActorDisabled(uint32_t aFormId) const noexcept override;
    bool IsActorDeleted(uint32_t aFormId) const noexcept override;
    void EnableActor(uint32_t aFormId) noexcept override;
    void DisableActor(uint32_t aFormId, bool aDelete) noexcept override;
    void ActorSpeakSound(uint32_t aFormId, const std::string& acFilename) noexcept override;
    void ActorStopCurrentDialogue(uint32_t aFormId) noexcept override;
    void ActorShowSubtitle(uint32_t aFormId, const std::string& acText, uint32_t aTopicInfoFormId, bool aForce) noexcept override;
    void SetActorIgnoreFriendlyHit(uint32_t aFormId, bool aIgnore) noexcept override;
    void SetActorPlayerRespawnMode(uint32_t aFormId) noexcept override;
    void SetActorCommandingActor(uint32_t aFormId, uint32_t aCommanderFormId) noexcept override;
    uint32_t GetActorCommandingActorFormId(uint32_t aFormId) const noexcept override;
    ActorAiFields GetActorAiFields(uint32_t aFormId) const noexcept override;
    void SetCopyAiFields(uint32_t aFormId, const ActorAiFields& acFields) noexcept override;
    void SetCopiesFightingPlayer(uint32_t aCount) noexcept override;
    void ActorLoadAnimationVariables(uint32_t aFormId, const AnimationVariables& acVars) noexcept override;
    void ActorSaveAnimationVariables(uint32_t aFormId, AnimationVariables& aVars) const noexcept override;
    std::string SaveActorGraphState(uint32_t aFormId) const noexcept override;
    bool LoadActorGraphState(uint32_t aFormId, const std::string& acState) noexcept override;
    ActionEvent GetActorLatestAnimation(uint32_t aFormId) const noexcept override;
    void SetActorLatestAnimation(uint32_t aFormId, const ActionEvent& acEvent) noexcept override;
    void ActorFixVampireLordModel(uint32_t aFormId) noexcept override;
    void ActorPutOnMount(uint32_t aRiderFormId, uint32_t aMountFormId) noexcept override;
    uint32_t GetActorParentCellFormId(uint32_t aFormId) const noexcept override;
    uint32_t GetActorWorldSpaceFormId(uint32_t aFormId) const noexcept override;
    uint32_t SpawnActorFromNpc(uint32_t aNpcFormId) noexcept override;
    uint32_t CreateCustomActor(const std::string& acAppearance, uint32_t aChangeFlags, float aWeight) noexcept override;
    void DeserializeActorNpc(uint32_t aFormId, const std::string& acAppearance, uint32_t aChangeFlags) noexcept override;
    uint32_t SerializeActorNpc(uint32_t aFormId, std::string& aBuffer) noexcept override;
    void MarkActorNpcChanged(uint32_t aFormId, uint32_t aFlags) noexcept override;
    uint32_t GetActorNpcTemplateBaseFormId(uint32_t aFormId) const noexcept override;
    bool IsActorNpcTemporary(uint32_t aFormId) const noexcept override;
    Tints GetActorFaceTints(uint32_t aFormId) const noexcept override;
    void SetupActorFaceGen(uint32_t aFormId) noexcept override;
    void SetupActorCache(uint32_t aFormId, uint32_t aEntityHint) noexcept override;
    void ActorUpdateInterpolation(uint32_t aFormId, uint64_t aTick) noexcept override;
    void ActorUpdateAnimation(uint32_t aFormId, uint64_t aTick) noexcept override;
    void ActorUpdateFaceGen(uint32_t aFormId) noexcept override;
    bool EffectHasHitVisuals(uint32_t aEffectFormId) const noexcept override;
    bool PlayEffectHitVisuals(uint32_t aFormId, uint32_t aEffectFormId) noexcept override;
    void StopEffectHitVisuals(uint32_t aFormId, uint32_t aEffectFormId) noexcept override;
    bool IsPlayingEffectHitVisuals(uint32_t aFormId, uint32_t aEffectFormId) const noexcept override;
    bool FlashEffectHitVisuals(uint32_t aFormId, uint32_t aEffectFormId) noexcept override;
    void PlayerKnockExplosion(uint32_t aFormId, float aX, float aY, float aZ, float aForce) noexcept override;

    // -----------------------------------------------------------------
    // IFormBridge
    // -----------------------------------------------------------------
    bool FormExists(uint32_t aFormId) const noexcept override;
    uint8_t GetFormType(uint32_t aFormId) const noexcept override;
    std::string GetFormName(uint32_t aFormId) const noexcept override;
    float GetGlobalValue(uint32_t aFormId) const noexcept override;
    void SetGlobalValue(uint32_t aFormId, float aValue) noexcept override;
    uint32_t LookupCellByCoords(uint32_t aWorldSpaceFormId, int32_t aX, int32_t aY) const noexcept override;
    uint32_t LookupWorldSpaceByFormId(uint32_t aFormId) const noexcept override;
    bool QuestExists(uint32_t aFormId) const noexcept override;
    uint32_t GetQuestStage(uint32_t aFormId) const noexcept override;
    bool IsQuestStopped(uint32_t aFormId) const noexcept override;
    uint8_t GetQuestType(uint32_t aFormId) const noexcept override;
    std::string GetQuestName(uint32_t aFormId) const noexcept override;
    void SetQuestActive(uint32_t aFormId, bool aActive) noexcept override;
    uint32_t FindQuestByNameId(const std::string& aNameId) const noexcept override;
    void ScriptSetQuestStage(uint32_t aFormId, uint32_t aStage) noexcept override;
    void SetQuestStopped(uint32_t aFormId) noexcept override;
    bool IsQuestStagesEmpty(uint32_t aFormId) const noexcept override;
    uint32_t GetModCount() const noexcept override;
    uint32_t GetModFormIdBySpellId(uint32_t aModStandardId, uint32_t aSpellLocalId) const noexcept override;
    std::vector<ModEntry> GetLoadedMods() const noexcept override;
    bool IsSpellItem(uint32_t aFormId) const noexcept override;
    bool IsConcentrationSpell(uint32_t aFormId) const noexcept override;
    bool IsHealingSpell(uint32_t aFormId) const noexcept override;
    bool IsBuffSpell(uint32_t aFormId) const noexcept override;
    bool IsWardSpell(uint32_t aFormId) const noexcept override;
    bool IsInvisibilitySpell(uint32_t aFormId) const noexcept override;
    bool IsMagicItem(uint32_t aFormId) const noexcept override;
    bool IsBoundWeaponSpell(uint32_t aFormId) const noexcept override;
    uint32_t GetSpellCastingType(uint32_t aFormId) const noexcept override;
    bool IsSpellEffectWerewolf(uint32_t aSpellFormId, uint32_t aEffectFormId) const noexcept override;
    bool IsSpellEffectVampireLord(uint32_t aSpellFormId, uint32_t aEffectFormId) const noexcept override;
    bool IsSpellEffectSlow(uint32_t aSpellFormId, uint32_t aEffectFormId) const noexcept override;
    void AddMagicTargetEffect(uint32_t aTargetFormId, uint32_t aCasterFormId,
                               uint32_t aSpellFormId, uint32_t aEffectFormId,
                               float aMagnitude, bool aIsDualCasting,
                               bool aApplyHealPerkBonus, bool aApplyStaminaPerkBonus) noexcept override;
    // --- Object operations ---
    bool ObjectHasLock(uint32_t aObjectFormId) const noexcept override;
    bool ObjectIsLocked(uint32_t aObjectFormId) const noexcept override;
    uint8_t GetObjectLockLevel(uint32_t aObjectFormId) const noexcept override;
    bool SetObjectLock(uint32_t aObjectFormId, bool aLocked, uint8_t aLockLevel) noexcept override;
    uint32_t GetObjectParentCellId(uint32_t aObjectFormId) const noexcept override;
    void ActivateObjectFromActor(uint32_t aObjectFormId, uint32_t aActivatorFormId, uint8_t aPreActivationState) noexcept override;
    uint8_t GetObjectOpenState(uint32_t aObjectFormId) const noexcept override;
    bool IsObjectBaseFormType(uint32_t aObjectFormId, uint8_t aFormType) const noexcept override;
    bool IsLoadDoor(uint32_t aObjectFormId) const noexcept override;
    Inventory GetObjectInventory(uint32_t aObjectFormId) const noexcept override;
    void SetObjectInventoryRetainingQuestItems(uint32_t aObjectFormId, const Inventory& aCurrentInventory, const Inventory& aNewInventory) noexcept override;
    void SetObjectInventory(uint32_t aObjectFormId, const Inventory& aInventory) noexcept override;
    bool IsRightHandEquipSlot(uint32_t aEquipSlotFormId) const noexcept override;
    // --- Projectile launch ---
    void LaunchProjectile(uint32_t aShooterFormId, uint32_t aCellFormId,
                           float aOriginX, float aOriginY, float aOriginZ,
                           uint32_t aProjectileBaseId, uint32_t aWeaponId, uint32_t aAmmoId,
                           uint32_t aSpellId, uint32_t aCastingSource,
                           float aZAngle, float aXAngle, float aYAngle,
                           int32_t aArea, float aPower, float aScale,
                           bool aAlwaysHit, bool aNoDamageOutsideCombat, bool aAutoAim,
                           bool aDeferInit, bool aForceConeOfFire,
                           bool aUnkBool1, bool aUnkBool2) noexcept override;
    // --- Actor inventory / equipment helpers ---
    Inventory GetActorEquipment(uint32_t aActorFormId) const noexcept override;
    void ActorDropOrPickUpObject(uint32_t aActorFormId, const Inventory::Entry& aItem) noexcept override;
    void ObjectAddOrRemoveItem(uint32_t aObjectFormId, const Inventory::Entry& aItem) noexcept override;
    Inventory GetActorWornArmor(uint32_t aActorFormId) const noexcept override;
    bool ActorIsWearingBodyPiece(uint32_t aActorFormId) const noexcept override;
    bool ActorShouldWearBodyPiece(uint32_t aActorFormId) const noexcept override;
    void ActorResetInventory(uint32_t aActorFormId, bool aBroadcast) noexcept override;
    void ActorEquipSpell(uint32_t aActorFormId, uint32_t aSpellFormId, uint32_t aSlotId) noexcept override;
    void ActorUnequipSpell(uint32_t aActorFormId, uint32_t aSpellFormId, uint32_t aSlotId) noexcept override;
    void ActorEquipShout(uint32_t aActorFormId, uint32_t aShoutFormId) noexcept override;
    void ActorUnequipShout(uint32_t aActorFormId, uint32_t aShoutFormId) noexcept override;
    void ActorEquipItem(uint32_t aActorFormId, uint32_t aItemFormId, uint32_t aEquipSlotFormId, int32_t aCount) noexcept override;
    void ActorUnequipItem(uint32_t aActorFormId, uint32_t aItemFormId, uint32_t aEquipSlotFormId, int32_t aCount) noexcept override;

    // -----------------------------------------------------------------
    // IPlayerBridge
    // -----------------------------------------------------------------
    uint32_t GetPlayerFormId() const noexcept override;
    uint32_t GetPlayerBaseNpcFormId() const noexcept override;
    void GetPlayerPosition(float& aX, float& aY, float& aZ) const noexcept override;
    uint32_t GetPlayerCellId() const noexcept override;
    uint32_t GetPlayerParentCellId() const noexcept override;
    uint32_t GetPlayerWorldSpaceId() const noexcept override;
    void SetPlayerWaypoint(float aX, float aY, float aZ, uint32_t aWorldSpaceFormId) noexcept override;
    void RemovePlayerWaypoint() noexcept override;
    std::string GetPlayerLocationName() const noexcept override;
    std::string GetPlayerWorldSpaceName() const noexcept override;
    bool IsPlayerAlive() const noexcept override;
    bool IsPlayerBleedingOut() const noexcept override;
    void SetPlayerGodMode(bool aEnable) noexcept override;
    void SetPlayerRespawnMode(bool aEnable) noexcept override;
    void RespawnPlayer() noexcept override;
    void KnockdownPlayer(float aForce) noexcept override;
    void FadeScreen(bool aFadeOut, bool aToBlack, float aFadeDuration,
                     bool aWithSound, float aDelay) noexcept override;
    void PlayerPayCrimeGoldToAllFactions() noexcept override;
    uint32_t GetDifficulty() const noexcept override;
    void SetDifficulty(uint32_t aDifficulty) noexcept override;
    float GetGreetDistance() const noexcept override;
    void SetGreetDistance(float aDistance) noexcept override;
    uint16_t GetPlayerLevel() const noexcept override;
    float GetPlayerActorValue(int32_t aIndex) const noexcept override;
    void ForcePlayerActorValue(int32_t aForceMode, int32_t aIndex, float aValue) noexcept override;
    uint32_t GetPlayerActiveMagicItemFormId(uint32_t aSlot) const noexcept override;
    uint32_t GetPlayerEquippedShoutFormId() const noexcept override;
    void EquipSpell(uint32_t aSpellFormId, uint32_t aSlot) noexcept override;
    void EquipShout(uint32_t aShoutFormId) noexcept override;
    uint32_t GetPlayerRaceFormId() const noexcept override;
    std::vector<uint32_t> GetPlayerActiveQuestFormIds() const noexcept override;
    bool IsPlayerLoaded() const noexcept override;
    bool ConsumeTeleported() noexcept override;
    float GetPlayerHeading() const noexcept override;
    void SetPlayerHeading(float aHeading) noexcept override;
    void SetPassThrough(const std::vector<uint32_t>& acActorFormIds) noexcept override;
    void StopTeleportEvents() noexcept override;
    void ShowHudMessage(const std::string& aMessage) noexcept override;
    void ShowEnemyHealth(uint32_t aActorFormId) noexcept override;
    Tints GetPlayerFaceTints() const noexcept override;
    void AddPlayerSkillExperience(int32_t aSkillId, float aExperience) noexcept override;
    int32_t GetPlayerLastUsedCombatSkill() const noexcept override;
    uint32_t GetPlayerParentCellFormId() const noexcept override;
    bool IsActorBleedingOut(uint32_t aFormId) const noexcept override;

    // -----------------------------------------------------------------
    // IWorldBridge
    // (GetModCount is shared with IFormBridge; a single override satisfies both interfaces.)
    // -----------------------------------------------------------------
    uint32_t GetCurrentWeatherId() const noexcept override;
    float GetGameTimeHours() const noexcept override;
    void SetSharedTimeScale(float aMultiplier) noexcept override;
    float GetGameTimeScale() const noexcept override;
    TimeModel GetGameTime() const noexcept override;
    float GetGameYear() const noexcept override;
    float GetGameMonth() const noexcept override;
    float GetGameDay() const noexcept override;

};
