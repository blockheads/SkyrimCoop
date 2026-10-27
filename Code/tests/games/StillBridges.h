#pragma once

// The bridges a game's services read the engine through, standing still: every call does nothing or returns nothing,
// but who the player is. Generated once from the interfaces; an interface's new method fails the build here, add it.
#include <Bridges/IActorBridge.h>
#include <Bridges/IFormBridge.h>
#include <Bridges/IPlayerBridge.h>
#include <Bridges/IWorldBridge.h>

#include <string>

struct StillBridges : IActorBridge, IPlayerBridge, IFormBridge, IWorldBridge
{
    std::string Name;
    uint16_t Level = 1;

    bool OpenFollowerInventory(uint32_t) noexcept override { return false; }
    uint32_t GetPlayerFormId() const noexcept override
    {
        return {};
    }
    uint32_t GetPlayerBaseNpcFormId() const noexcept override
    {
        return 0x7;
    }
    void GetPlayerPosition(float& aX, float& aY, float& aZ) const noexcept override
    {
    }
    uint32_t GetPlayerCellId() const noexcept override
    {
        return {};
    }
    uint32_t GetPlayerParentCellId() const noexcept override
    {
        return {};
    }
    uint32_t GetPlayerWorldSpaceId() const noexcept override
    {
        return {};
    }
    void SetPlayerWaypoint(float aX, float aY, float aZ, uint32_t aWorldSpaceFormId) noexcept override
    {
    }
    void RemovePlayerWaypoint() noexcept override
    {
    }
    std::string GetPlayerLocationName() const noexcept override
    {
        return {};
    }
    std::string GetPlayerWorldSpaceName() const noexcept override
    {
        return {};
    }
    bool IsPlayerMounted() const noexcept override { return false; }
    bool IsPlayerAlive() const noexcept override
    {
        return {};
    }
    void HoldPlayerDeathReload(bool) noexcept override
    {
    }
    void RevivePlayer() noexcept override
    {
    }
    void SetSpectatorTarget(uint32_t) noexcept override {}
    void KeepSpectatorView() noexcept override {}
    void HoldDeathControls(bool) noexcept override {}
    bool IsScriptCameraHeld() const noexcept override { return false; }
    uint32_t GetDifficulty() const noexcept override
    {
        return {};
    }
    void SetDifficulty(uint32_t aDifficulty) noexcept override
    {
    }
    float GetGreetDistance() const noexcept override
    {
        return {};
    }
    void SetGreetDistance(float aDistance) noexcept override
    {
    }
    uint16_t GetPlayerLevel() const noexcept override
    {
        return Level;
    }
    float GetPlayerActorValue(int32_t aIndex) const noexcept override
    {
        return {};
    }
    void ForcePlayerActorValue(int32_t aForceMode, int32_t aIndex, float aValue) noexcept override
    {
    }
    uint32_t GetPlayerActiveMagicItemFormId(uint32_t aSlot) const noexcept override
    {
        return {};
    }
    uint32_t GetPlayerEquippedShoutFormId() const noexcept override
    {
        return {};
    }
    void EquipSpell(uint32_t aSpellFormId, uint32_t aSlot) noexcept override
    {
    }
    void EquipShout(uint32_t aShoutFormId) noexcept override
    {
    }
    uint32_t GetPlayerRaceFormId() const noexcept override
    {
        return {};
    }
    std::vector<uint32_t> GetPlayerActiveQuestFormIds() const noexcept override
    {
        return {};
    }
    bool IsPlayerLoaded() const noexcept override
    {
        return {};
    }
    bool ConsumeTeleported() noexcept override
    {
        return {};
    }
    float GetPlayerHeading() const noexcept override
    {
        return {};
    }
    void SetPlayerHeading(float aHeading) noexcept override
    {
    }
    void SetPassThrough(const std::vector<uint32_t>& acActorFormIds) noexcept override
    {
    }
    void StopTeleportEvents() noexcept override
    {
    }
    void ShowHudMessage(const std::string& aMessage) noexcept override
    {
    }
    void ShowEnemyHealth(uint32_t aActorFormId) noexcept override
    {
    }
    Tints GetPlayerFaceTints() const noexcept override
    {
        return {};
    }
    uint32_t GetPlayerParentCellFormId() const noexcept override
    {
        return {};
    }
    bool IsActorBleedingOut(uint32_t aFormId) const noexcept override
    {
        return {};
    }

    bool FormExists(uint32_t aFormId) const noexcept override
    {
        return {};
    }
    uint8_t GetFormType(uint32_t aFormId) const noexcept override
    {
        return {};
    }
    std::string GetFormName(uint32_t aFormId) const noexcept override
    {
        return Name;
    }
    float GetGlobalValue(uint32_t aFormId) const noexcept override
    {
        return {};
    }
    void SetGlobalValue(uint32_t aFormId, float aValue) noexcept override
    {
    }
    uint32_t LookupCellByCoords(uint32_t aWorldSpaceFormId, int32_t aX, int32_t aY) const noexcept override
    {
        return {};
    }
    uint32_t LookupWorldSpaceByFormId(uint32_t aFormId) const noexcept override
    {
        return {};
    }
    bool QuestExists(uint32_t aFormId) const noexcept override
    {
        return {};
    }
    uint32_t GetQuestStage(uint32_t aFormId) const noexcept override
    {
        return {};
    }
    bool IsQuestStopped(uint32_t aFormId) const noexcept override
    {
        return {};
    }
    uint8_t GetQuestType(uint32_t aFormId) const noexcept override
    {
        return {};
    }
    std::string GetQuestName(uint32_t aFormId) const noexcept override
    {
        return {};
    }
    void SetQuestActive(uint32_t aFormId, bool aActive) noexcept override
    {
    }
    uint32_t FindQuestByNameId(const std::string& aNameId) const noexcept override
    {
        return {};
    }
    void ScriptSetQuestStage(uint32_t aFormId, uint32_t aStage) noexcept override
    {
    }
    void SetQuestStopped(uint32_t aFormId) noexcept override
    {
    }
    bool IsQuestStagesEmpty(uint32_t aFormId) const noexcept override
    {
        return {};
    }
    uint32_t GetModCount() const noexcept override
    {
        return {};
    }
    uint32_t GetModFormIdBySpellId(uint32_t aModStandardId, uint32_t aSpellLocalId) const noexcept override
    {
        return {};
    }
    std::vector<ModEntry> GetLoadedMods() const noexcept override
    {
        return {};
    }
    bool IsSpellItem(uint32_t aFormId) const noexcept override
    {
        return {};
    }
    bool IsConcentrationSpell(uint32_t aFormId) const noexcept override
    {
        return {};
    }
    bool IsHealingSpell(uint32_t aFormId) const noexcept override
    {
        return {};
    }
    bool IsBuffSpell(uint32_t aFormId) const noexcept override
    {
        return {};
    }
    bool IsWardSpell(uint32_t aFormId) const noexcept override
    {
        return {};
    }
    bool IsInvisibilitySpell(uint32_t aFormId) const noexcept override
    {
        return {};
    }
    bool IsBoundWeaponSpell(uint32_t aFormId) const noexcept override
    {
        return {};
    }
    bool IsMagicItem(uint32_t aFormId) const noexcept override
    {
        return {};
    }
    uint32_t GetSpellCastingType(uint32_t aFormId) const noexcept override
    {
        return {};
    }
    bool IsSpellEffectWerewolf(uint32_t aSpellFormId, uint32_t aEffectFormId) const noexcept override
    {
        return {};
    }
    bool IsSpellEffectVampireLord(uint32_t aSpellFormId, uint32_t aEffectFormId) const noexcept override
    {
        return {};
    }
    bool IsSpellEffectSlow(uint32_t aSpellFormId, uint32_t aEffectFormId) const noexcept override
    {
        return {};
    }
    void AddMagicTargetEffect(uint32_t aTargetFormId, uint32_t aCasterFormId, uint32_t aSpellFormId,
                              uint32_t aEffectFormId, float aMagnitude, bool aIsDualCasting, bool aApplyHealPerkBonus,
                              bool aApplyStaminaPerkBonus) noexcept override
    {
    }
    bool ObjectHasLock(uint32_t aObjectFormId) const noexcept override
    {
        return {};
    }
    bool ObjectIsLocked(uint32_t aObjectFormId) const noexcept override
    {
        return {};
    }
    uint8_t GetObjectLockLevel(uint32_t aObjectFormId) const noexcept override
    {
        return {};
    }
    bool SetObjectLock(uint32_t aObjectFormId, bool aLocked, uint8_t aLockLevel) noexcept override
    {
        return {};
    }
    uint32_t GetObjectParentCellId(uint32_t aObjectFormId) const noexcept override
    {
        return {};
    }
    void ActivateObjectFromActor(uint32_t aObjectFormId, uint32_t aActivatorFormId,
                                 uint8_t aPreActivationState) noexcept override
    {
    }
    uint8_t GetObjectOpenState(uint32_t aObjectFormId) const noexcept override
    {
        return {};
    }
    bool IsObjectBaseFormType(uint32_t aObjectFormId, uint8_t aFormType) const noexcept override
    {
        return {};
    }
    bool IsLoadDoor(uint32_t aObjectFormId) const noexcept override
    {
        return {};
    }
    Inventory GetObjectInventory(uint32_t aObjectFormId) const noexcept override
    {
        return {};
    }
    void SetObjectInventoryRetainingQuestItems(uint32_t aObjectFormId, const Inventory& aCurrentInventory,
                                               const Inventory& aNewInventory) noexcept override
    {
    }
    void SetObjectInventory(uint32_t aObjectFormId, const Inventory& aInventory) noexcept override
    {
    }
    bool IsRightHandEquipSlot(uint32_t aEquipSlotFormId) const noexcept override
    {
        return {};
    }
    void LaunchProjectile(uint32_t aShooterFormId, uint32_t aCellFormId, float aOriginX, float aOriginY, float aOriginZ,
                          uint32_t aProjectileBaseId, uint32_t aWeaponId, uint32_t aAmmoId, uint32_t aSpellId,
                          uint32_t aCastingSource, float aZAngle, float aXAngle, float aYAngle, int32_t aArea,
                          float aPower, float aScale, bool aAlwaysHit, bool aNoDamageOutsideCombat, bool aAutoAim,
                          bool aDeferInit, bool aForceConeOfFire, bool aUnkBool1, bool aUnkBool2) noexcept override
    {
    }
    Inventory GetActorEquipment(uint32_t aActorFormId) const noexcept override
    {
        return {};
    }
    void ActorDropOrPickUpObject(uint32_t aActorFormId, const Inventory::Entry& aItem) noexcept override
    {
    }
    void ObjectAddOrRemoveItem(uint32_t aObjectFormId, const Inventory::Entry& aItem) noexcept override
    {
    }
    Inventory GetActorWornArmor(uint32_t aActorFormId) const noexcept override
    {
        return {};
    }
    bool ActorIsWearingBodyPiece(uint32_t aActorFormId) const noexcept override
    {
        return {};
    }
    bool ActorShouldWearBodyPiece(uint32_t aActorFormId) const noexcept override
    {
        return {};
    }
    void ActorResetInventory(uint32_t aActorFormId, bool aBroadcast) noexcept override
    {
    }
    void ActorEquipSpell(uint32_t aActorFormId, uint32_t aSpellFormId, uint32_t aSlotId) noexcept override
    {
    }
    void ActorUnequipSpell(uint32_t aActorFormId, uint32_t aSpellFormId, uint32_t aSlotId) noexcept override
    {
    }
    void ActorEquipShout(uint32_t aActorFormId, uint32_t aShoutFormId) noexcept override
    {
    }
    void ActorUnequipShout(uint32_t aActorFormId, uint32_t aShoutFormId) noexcept override
    {
    }
    void ActorEquipItem(uint32_t aActorFormId, uint32_t aItemFormId, uint32_t aEquipSlotFormId,
                        int32_t aCount) noexcept override
    {
    }
    void ActorUnequipItem(uint32_t aActorFormId, uint32_t aItemFormId, uint32_t aEquipSlotFormId,
                          int32_t aCount) noexcept override
    {
    }

    uint32_t GetCurrentWeatherId() const noexcept override
    {
        return {};
    }
    float GetGameTimeHours() const noexcept override
    {
        return {};
    }
    void SetSharedTimeScale(float aMultiplier) noexcept override
    {
    }
    float GetGameTimeScale() const noexcept override
    {
        return {};
    }
    float GetGameYear() const noexcept override
    {
        return {};
    }
    float GetGameMonth() const noexcept override
    {
        return {};
    }
    float GetGameDay() const noexcept override
    {
        return {};
    }
    TimeModel GetGameTime() const noexcept override
    {
        return {};
    }

    // IActorBridge
    bool ActorExists(uint32_t aFormId) const noexcept override
    {
        return {};
    }
    bool IsActorDead(uint32_t aFormId) const noexcept override
    {
        return {};
    }
    bool IsPlayerSummon(uint32_t aFormId) const noexcept override
    {
        return {};
    }
    bool IsTemporaryActor(uint32_t aFormId) const noexcept override
    {
        return {};
    }
    bool IsOtherPlayer(uint32_t aFormId) const noexcept override
    {
        return {};
    }
    void SetActorCopy(uint32_t aFormId, bool aCopy) noexcept override
    {
    }
    bool SetActorPersistent(uint32_t aFormId, bool aPersistent = true) noexcept override
    {
        return false;
    }
    bool IsActorCopy(uint32_t aFormId) const noexcept override
    {
        return {};
    }
    void SetActorPlayer(uint32_t aFormId, bool aPlayer) noexcept override
    {
    }
    uint32_t GetCommandingActor(uint32_t aFormId) const noexcept override
    {
        return {};
    }
    void KillActor(uint32_t aFormId) noexcept override
    {
    }
    bool DamageActorHealth(uint32_t aFormId, uint32_t aAttackerFormId, float aDamage) noexcept override
    {
        return {};
    }
    void RespawnActor(uint32_t aFormId) noexcept override
    {
    }
    void ResurrectActor(uint32_t aFormId) noexcept override
    {
    }
    void ResumeActorAnimation(uint32_t aFormId) noexcept override
    {
    }
    void DeleteActor(uint32_t aFormId) noexcept override
    {
    }
    void MoveActorTo(uint32_t aFormId, uint32_t aCellId, float aX, float aY, float aZ) noexcept override
    {
    }
    void SetActorRotation(uint32_t aFormId, float aX, float aZ) noexcept override
    {
    }
    bool ApplyRemoteMovement(uint32_t aFormId, const float aPosition[3], const float aRotation[3], float aDirection,
                             const AnimationVariables& aVariables, bool aPlace) noexcept override
    {
        return {};
    }
    bool IsLyingInRagdoll(uint32_t) const noexcept override
    {
        return false;
    }
    void GetActorPosition(uint32_t aFormId, float& aX, float& aY, float& aZ) const noexcept override
    {
    }
    void GetActorRotation(uint32_t aFormId, float& aX, float& aY, float& aZ) const noexcept override
    {
    }
    float GetActorValue(uint32_t aFormId, int32_t aIndex) const noexcept override
    {
        return {};
    }
    float GetActorPermanentValue(uint32_t aFormId, int32_t aIndex) const noexcept override
    {
        return {};
    }
    void SetActorValue(uint32_t aFormId, int32_t aIndex, float aValue) noexcept override
    {
    }
    void ForceActorValue(uint32_t aFormId, int32_t aForceMode, int32_t aIndex, float aValue) noexcept override
    {
    }
    int32_t GetActorValueCount() const noexcept override
    {
        return {};
    }
    bool IsWeaponDrawn(uint32_t aFormId) const noexcept override
    {
        return {};
    }
    uint32_t GetEquippedWeaponFormId(uint32_t aActorFormId, uint32_t aSlot) const noexcept override
    {
        return {};
    }
    uint64_t GetAnimationGraphDescriptorHash(uint32_t aFormId) const noexcept override
    {
        return {};
    }
    void SetAnimationGraphDescriptorHash(uint32_t aFormId, uint64_t aHash) noexcept override
    {
    }
    void GenerateMagicCasters(uint32_t aFormId) noexcept override
    {
    }
    void SetMagicCasterDualCasting(uint32_t aFormId, uint32_t aCastingSource, bool aDual) noexcept override
    {
    }
    void CastSpell(uint32_t aActorFormId, uint32_t aSpellFormId, uint32_t aCastingSource,
                   uint32_t aDesiredTargetFormId) noexcept override
    {
    }
    void InterruptCast(uint32_t aFormId, uint32_t aCastingSource) noexcept override
    {
    }
    uint32_t GetCastingSpellFormId(uint32_t aFormId, uint32_t aCastingSource) const noexcept override
    {
        return {};
    }
    void RemoveSpell(uint32_t aActorFormId, uint32_t aSpellFormId) noexcept override
    {
    }
    bool IsBleedingOut(uint32_t aFormId) const noexcept override
    {
        return {};
    }
    void PayCrimeGoldToAllFactions(uint32_t aFormId) noexcept override
    {
    }
    uint32_t GetEquippedShoutFormId(uint32_t aFormId) const noexcept override
    {
        return {};
    }
    uint32_t GetActiveMagicItemFormId(uint32_t aFormId, uint32_t aSlot) const noexcept override
    {
        return {};
    }
    uint32_t GetCombatTargetFormId(uint32_t aFormId) const noexcept override
    {
        return {};
    }
    uint32_t GetActorBaseFormId(uint32_t aFormId) const noexcept override
    {
        return {};
    }
    bool SetActorBaseFormId(uint32_t aFormId, uint32_t aBaseFormId) noexcept override
    {
        return false;
    }
    std::string GetActorBaseName(uint32_t aFormId) const noexcept override
    {
        return {};
    }
    void SetActorBaseLevel(uint32_t, uint16_t) noexcept override {}
    float GetActorBaseWeight(uint32_t aFormId) const noexcept override
    {
        return 50.f;
    }
    uint32_t GetActorRaceFormId(uint32_t aFormId) const noexcept override
    {
        return {};
    }
    bool ActorHas3D(uint32_t aFormId) const noexcept override
    {
        return {};
    }
    bool IsAnimationGraphReady(uint32_t aFormId) const noexcept override
    {
        return {};
    }
    std::vector<ActiveEffectInfo> GetActorActiveEffects(uint32_t aFormId) const noexcept override
    {
        return {};
    }
    void ReactToAttack(uint32_t aVictimFormId, uint32_t aAttackerFormId) noexcept override
    {
    }
    uint32_t GetKillerFormId(uint32_t aFormId) const noexcept override
    {
        return {};
    }
    std::vector<uint32_t> GetLoadedActorFormIds() const noexcept override
    {
        return {};
    }
    ActorValues GetActorEssentialValues(uint32_t aFormId) const noexcept override
    {
        return {};
    }
    void SetActorValues(uint32_t aFormId, const ActorValues& acValues) noexcept override
    {
    }
    Inventory GetActorInventory(uint32_t aFormId) const noexcept override
    {
        return {};
    }
    void SetActorInventory(uint32_t aFormId, const Inventory& acInventory) noexcept override
    {
    }
    Factions GetActorFactions(uint32_t aFormId) const noexcept override
    {
        return {};
    }
    void SetActorFactions(uint32_t aFormId, const Factions& acFactions) noexcept override
    {
    }
    void SetActorWeaponDrawnEx(uint32_t aFormId, bool aDraw) noexcept override
    {
    }
    WeaponState GetWeaponState(uint32_t aFormId) const noexcept override
    {
        return {};
    }
    bool IsActorTemporary(uint32_t aFormId) const noexcept override
    {
        return {};
    }
    bool IsActorMount(uint32_t aFormId) const noexcept override
    {
        return {};
    }
    bool IsActorDragon(uint32_t aFormId) const noexcept override
    {
        return {};
    }
    bool IsActorVampireLord(uint32_t aFormId) const noexcept override
    {
        return {};
    }
    bool IsActorDisabled(uint32_t aFormId) const noexcept override
    {
        return {};
    }
    bool IsActorDeleted(uint32_t aFormId) const noexcept override
    {
        return {};
    }
    void EnableActor(uint32_t aFormId) noexcept override
    {
    }
    void DisableActor(uint32_t aFormId, bool aDelete) noexcept override
    {
    }
    void ActorSpeakSound(uint32_t aFormId, const std::string& acFilename) noexcept override
    {
    }
    void ActorStopCurrentDialogue(uint32_t aFormId) noexcept override
    {
    }
    void ActorShowSubtitle(uint32_t aFormId, const std::string& acText, uint32_t aTopicInfoFormId,
                           bool aForce) noexcept override
    {
    }
    void SetActorIgnoreFriendlyHit(uint32_t aFormId, bool aIgnore) noexcept override
    {
    }
    void SetActorPlayerFaction(uint32_t aFormId) noexcept override
    {
    }
    void SetActorCommandingActor(uint32_t aFormId, uint32_t aCommanderFormId) noexcept override
    {
    }
    uint32_t GetActorCommandingActorFormId(uint32_t aFormId) const noexcept override
    {
        return {};
    }
    ActorAiFields GetActorAiFields(uint32_t aFormId) const noexcept override
    {
        return {};
    }
    void SetCopyAiFields(uint32_t aFormId, const ActorAiFields& acFields) noexcept override
    {
    }
    void SetCopiesFightingPlayer(uint32_t aCount) noexcept override
    {
    }
    void ActorLoadAnimationVariables(uint32_t aFormId, const AnimationVariables& acVars) noexcept override
    {
    }
    void ActorSaveAnimationVariables(uint32_t aFormId, AnimationVariables& aVars) const noexcept override
    {
    }
    std::string SaveActorGraphState(uint32_t aFormId) const noexcept override
    {
        return {};
    }
    bool LoadActorGraphState(uint32_t aFormId, const std::string& acState) noexcept override
    {
        return {};
    }
    ActionEvent GetActorLatestAnimation(uint32_t aFormId) const noexcept override
    {
        return {};
    }
    void SetActorLatestAnimation(uint32_t aFormId, const ActionEvent& acEvent) noexcept override
    {
    }
    void ActorFixVampireLordModel(uint32_t aFormId) noexcept override
    {
    }
    void ActorPutOnMount(uint32_t aRiderFormId, uint32_t aMountFormId) noexcept override
    {
    }
    uint32_t GetActorParentCellFormId(uint32_t aFormId) const noexcept override
    {
        return {};
    }
    uint32_t GetActorWorldSpaceFormId(uint32_t aFormId) const noexcept override
    {
        return {};
    }
    uint32_t SpawnActorFromNpc(uint32_t aNpcFormId) noexcept override
    {
        return {};
    }
    uint32_t CreateCustomActor(const std::string& acAppearance, uint32_t aChangeFlags, float aWeight) noexcept override
    {
        return {};
    }
    void DeserializeActorNpc(uint32_t aFormId, const std::string& acAppearance, uint32_t aChangeFlags) noexcept override
    {
    }
    bool ApplyActorLooks(uint32_t, const std::string&, uint32_t, float) noexcept override { return false; }
    uint32_t SerializeActorNpc(uint32_t aFormId, std::string& aBuffer) noexcept override
    {
        return {};
    }
    void MarkActorNpcChanged(uint32_t aFormId, uint32_t aFlags) noexcept override
    {
    }
    uint32_t GetActorNpcTemplateBaseFormId(uint32_t aFormId) const noexcept override
    {
        return {};
    }
    bool IsActorNpcTemporary(uint32_t aFormId) const noexcept override
    {
        return {};
    }
    Tints GetActorFaceTints(uint32_t aFormId) const noexcept override
    {
        return {};
    }
    void SetupActorFaceGen(uint32_t aFormId) noexcept override
    {
    }
    void SetupActorCache(uint32_t aFormId, uint32_t aEntityHint) noexcept override
    {
    }
    void ActorUpdateInterpolation(uint32_t aFormId, uint64_t aTick) noexcept override
    {
    }
    void ActorUpdateAnimation(uint32_t aFormId, uint64_t aTick) noexcept override
    {
    }
    void ActorUpdateFaceGen(uint32_t aFormId) noexcept override
    {
    }
    bool EffectHasHitVisuals(uint32_t aEffectFormId) const noexcept override
    {
        return {};
    }
    bool PlayEffectHitVisuals(uint32_t aFormId, uint32_t aEffectFormId) noexcept override
    {
        return {};
    }
    void StopEffectHitVisuals(uint32_t aFormId, uint32_t aEffectFormId) noexcept override
    {
    }
    bool IsPlayingEffectHitVisuals(uint32_t aFormId, uint32_t aEffectFormId) const noexcept override
    {
        return {};
    }
    bool FlashEffectHitVisuals(uint32_t aFormId, uint32_t aEffectFormId) noexcept override
    {
        return {};
    }
};
