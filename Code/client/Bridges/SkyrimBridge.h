#pragma once

#include <Bridges/IActorBridge.h>
#include <Bridges/IFormBridge.h>
#include <Bridges/IPlayerBridge.h>
#include <Bridges/IEventBridge.h>
#include <Bridges/IWorldBridge.h>

#include <atomic>
#include <chrono>
#include <fstream>
#include <mutex>
#include <string>

/// Real SKSE implementation of all five bridge interfaces.
/// This is the ONLY file (along with SkyrimBridge.cpp) that is allowed to
/// include SKSE headers (Actor.h, PlayerCharacter.h, Sky.h, etc.).
/// Compiled only as part of SkyrimTogetherClient — never into test targets.
class SkyrimBridge
    : public IActorBridge
    , public IFormBridge
    , public IPlayerBridge
    , public IEventBridge
    , public IWorldBridge
{
public:
    SkyrimBridge() = default;
    ~SkyrimBridge() override;

    // -----------------------------------------------------------------
    // IActorBridge
    // -----------------------------------------------------------------
    bool ActorExists(uint32_t aFormId) const noexcept override;
    bool IsActorDead(uint32_t aFormId) const noexcept override;
    bool IsPlayerSummon(uint32_t aFormId) const noexcept override;
    bool IsTemporaryActor(uint32_t aFormId) const noexcept override;
    bool IsRemotePlayer(uint32_t aFormId) const noexcept override;
    void SetActorRemote(uint32_t aFormId, bool aRemote) noexcept override;
    void SetActorPlayer(uint32_t aFormId, bool aPlayer) noexcept override;
    uint32_t GetCommandingActor(uint32_t aFormId) const noexcept override;
    void KillActor(uint32_t aFormId) noexcept override;
    void RespawnActor(uint32_t aFormId) noexcept override;
    void DeleteActor(uint32_t aFormId) noexcept override;
    void MoveActorTo(uint32_t aFormId, uint32_t aCellId, float aX, float aY, float aZ) noexcept override;
    void SetActorRotation(uint32_t aFormId, float aX, float aZ) noexcept override;
    void GetActorPosition(uint32_t aFormId, float& aX, float& aY, float& aZ) const noexcept override;
    void GetActorRotation(uint32_t aFormId, float& aX, float& aY, float& aZ) const noexcept override;
    float GetActorValue(uint32_t aFormId, int32_t aIndex) const noexcept override;
    float GetActorPermanentValue(uint32_t aFormId, int32_t aIndex) const noexcept override;
    void SetActorValue(uint32_t aFormId, int32_t aIndex, float aValue) noexcept override;
    void ForceActorValue(uint32_t aFormId, int32_t aForceMode, int32_t aIndex, float aValue) noexcept override;
    int32_t GetActorValueCount() const noexcept override;
    void EquipItem(uint32_t aActorFormId, uint32_t aItemFormId, bool aUnequipSlot) noexcept override;
    void UnequipItem(uint32_t aActorFormId, uint32_t aItemFormId) noexcept override;
    void DrawWeapon(uint32_t aFormId, bool aDraw) noexcept override;
    bool IsWeaponDrawn(uint32_t aFormId) const noexcept override;
    uint32_t GetEquippedWeaponFormId(uint32_t aActorFormId, uint32_t aSlot) const noexcept override;
    uint64_t GetAnimationGraphDescriptorHash(uint32_t aFormId) const noexcept override;
    void SetAnimationGraphDescriptorHash(uint32_t aFormId, uint64_t aHash) noexcept override;
    void GenerateMagicCasters(uint32_t aFormId) noexcept override;
    void SetMagicCasterDualCasting(uint32_t aFormId, uint32_t aCastingSource, bool aDual) noexcept override;
    void CastSpell(uint32_t aActorFormId, uint32_t aSpellFormId, uint32_t aCastingSource) noexcept override;
    void InterruptCast(uint32_t aFormId, uint32_t aCastingSource) noexcept override;
    void RemoveSpell(uint32_t aActorFormId, uint32_t aSpellFormId) noexcept override;
    void AddMagicTarget(uint32_t aActorFormId, uint32_t aCasterFormId, uint32_t aTargetFormId,
                         bool aApplyHealPerkBonus, bool aApplyStaminaPerkBonus) noexcept override;
    bool IsBleedingOut(uint32_t aFormId) const noexcept override;
    void PayCrimeGoldToAllFactions(uint32_t aFormId) noexcept override;
    uint32_t GetEquippedShoutFormId(uint32_t aFormId) const noexcept override;
    uint32_t GetActiveMagicItemFormId(uint32_t aFormId, uint32_t aSlot) const noexcept override;
    uint32_t GetCombatTargetFormId(uint32_t aFormId) const noexcept override;
    uint32_t GetActorBaseFormId(uint32_t aFormId) const noexcept override;
    std::string GetActorBaseName(uint32_t aFormId) const noexcept override;
    uint32_t GetActorRaceFormId(uint32_t aFormId) const noexcept override;
    bool ActorHas3D(uint32_t aFormId) const noexcept override;

    // -----------------------------------------------------------------
    // IFormBridge
    // -----------------------------------------------------------------
    bool FormExists(uint32_t aFormId) const noexcept override;
    uint8_t GetFormType(uint32_t aFormId) const noexcept override;
    std::string GetFormEditorId(uint32_t aFormId) const noexcept override;
    std::string GetFormName(uint32_t aFormId) const noexcept override;
    float GetGlobalValue(uint32_t aFormId) const noexcept override;
    void SetGlobalValue(uint32_t aFormId, float aValue) noexcept override;
    uint32_t LookupCellByCoords(uint32_t aWorldSpaceFormId, int32_t aX, int32_t aY) const noexcept override;
    uint32_t LookupWorldSpaceByFormId(uint32_t aFormId) const noexcept override;
    uint32_t GetParentCellFormId(uint32_t aObjectFormId) const noexcept override;
    uint32_t GetParentWorldSpaceFormId(uint32_t aObjectFormId) const noexcept override;
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
    bool IsQuestOverrideActive() const noexcept override;
    uint32_t GetModCount() const noexcept override;
    std::string GetModNameByIndex(uint32_t aIndex) const noexcept override;
    uint32_t GetModStandardId(const std::string& aModName) const noexcept override;
    std::vector<std::string> GetAllModNames() const noexcept override;
    uint32_t GetModFormIdBySpellId(uint32_t aModStandardId, uint32_t aSpellLocalId) const noexcept override;
    std::vector<ModEntry> GetLoadedMods() const noexcept override;
    bool IsSpellItem(uint32_t aFormId) const noexcept override;
    bool IsConcentrationSpell(uint32_t aFormId) const noexcept override;
    bool IsHealingSpell(uint32_t aFormId) const noexcept override;
    bool IsWardSpell(uint32_t aFormId) const noexcept override;
    bool IsInvisibilitySpell(uint32_t aFormId) const noexcept override;
    bool IsMagicItem(uint32_t aFormId) const noexcept override;
    // --- Object operations ---
    bool ObjectHasLock(uint32_t aObjectFormId) const noexcept override;
    bool ObjectIsLocked(uint32_t aObjectFormId) const noexcept override;
    uint8_t GetObjectLockLevel(uint32_t aObjectFormId) const noexcept override;
    bool SetObjectLock(uint32_t aObjectFormId, bool aLocked, uint8_t aLockLevel) noexcept override;
    void NotifyObjectLockChange(uint32_t aObjectFormId) noexcept override;
    uint32_t GetObjectParentCellId(uint32_t aObjectFormId) const noexcept override;
    void ActivateObjectFromActor(uint32_t aObjectFormId, uint32_t aActivatorFormId, uint8_t aPreActivationState) noexcept override;
    void PlayObjectAnimation(uint32_t aObjectFormId, const std::string& aAnimation, const std::string& aEventName) noexcept override;
    uint8_t GetObjectOpenState(uint32_t aObjectFormId) const noexcept override;
    bool IsObjectBaseFormType(uint32_t aObjectFormId, uint8_t aFormType) const noexcept override;
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
    void MovePlayerTo(uint32_t aCellId, float aX, float aY, float aZ) noexcept override;
    std::string GetPlayerLocationName() const noexcept override;
    std::string GetPlayerWorldSpaceName() const noexcept override;
    bool IsPlayerAlive() const noexcept override;
    bool IsPlayerBleedingOut() const noexcept override;
    bool IsPlayerInGodMode() const noexcept override;
    void SetPlayerGodMode(bool aEnable) noexcept override;
    void SetPlayerRespawnMode(bool aEnable) noexcept override;
    void RespawnPlayer() noexcept override;
    void KnockdownPlayer(float aForce) noexcept override;
    void FadeScreen(bool aFadeOut, bool aToBlack, float aFadeDuration,
                     bool aWithSound, float aDelay) noexcept override;
    void PlayerPayGold(uint32_t aAmount) noexcept override;
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
    void ShowHudMessage(const std::string& aMessage) noexcept override;

    // -----------------------------------------------------------------
    // IEventBridge
    // -----------------------------------------------------------------
    void RegisterQuestStartStopSink(std::function<void(uint32_t, bool)> aCallback) noexcept override;
    void RegisterQuestStageSink(std::function<void(uint32_t, uint32_t)> aCallback) noexcept override;
    void RegisterActivateSink(std::function<void(uint32_t, uint32_t)> aCallback) noexcept override;
    void RegisterLockChangeSink(std::function<void(uint32_t, bool)> aCallback) noexcept override;
    void UnregisterAllSinks() noexcept override;
    void InjectQuestStartStopEvent(uint32_t aQuestFormId, bool aStarted) noexcept override;
    void InjectQuestStageEvent(uint32_t aQuestFormId, uint32_t aNewStage) noexcept override;
    void InjectActivateEvent(uint32_t aActivatorFormId, uint32_t aActivatedFormId) noexcept override;

    // -----------------------------------------------------------------
    // IWorldBridge
    // (GetModCount and GetAllModNames are shared with IFormBridge;
    //  a single override satisfies both interfaces.)
    // -----------------------------------------------------------------
    uint32_t GetCurrentWeatherId() const noexcept override;
    uint32_t GetLastWeatherId() const noexcept override;
    void ForceWeather(uint32_t aWeatherFormId, bool aOverride) noexcept override;
    void ReleaseWeatherOverride() noexcept override;
    std::string GetModName(uint32_t aIndex) const noexcept override;
    void SetGameWeatherSystemEnabled(bool aEnable) noexcept override;
    float GetGameTimeHours() const noexcept override;
    void SetGameTimeHours(float aHours) noexcept override;
    float GetGameTimeScale() const noexcept override;
    float GetGameYear() const noexcept override;
    float GetGameMonth() const noexcept override;
    float GetGameDay() const noexcept override;

    // -----------------------------------------------------------------
    // NDJSON recording infrastructure
    // -----------------------------------------------------------------
    void StartRecording(const std::string& aPath) noexcept;
    void StopRecording() noexcept;

private:
    void RecordCall(const char* apOp, const char* apPayloadJson) noexcept;
    void RecordReturn(const char* apOp, const char* apResultJson) noexcept;

    std::ofstream m_recordFile;
    std::mutex    m_recordMutex;
    std::chrono::steady_clock::time_point m_startTime{};
    bool m_recording = false;
};
