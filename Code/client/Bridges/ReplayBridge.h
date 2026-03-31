#pragma once

#include <Bridges/IActorBridge.h>
#include <Bridges/IFormBridge.h>
#include <Bridges/IPlayerBridge.h>
#include <Bridges/IEventBridge.h>
#include <Bridges/IWorldBridge.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Forward declare nlohmann::json so the header stays light
namespace nlohmann { class json; }

/// Headless replay implementation of all five bridge interfaces.
///
/// Does NOT include any SKSE header — only primitives and standard library.
/// Compiled into SkyrimTogetherClientServices (the boundary-proof static lib)
/// and into ClientReplayTests.
///
/// Behaviour: loads an NDJSON recording produced by SkyrimBridge::StartRecording(),
/// then returns canned responses in playback order, matching calls by "op" name.
class ReplayBridge
    : public IActorBridge
    , public IFormBridge
    , public IPlayerBridge
    , public IEventBridge
    , public IWorldBridge
{
public:
    ReplayBridge() = default;
    ~ReplayBridge() override = default;

    /// Load an NDJSON recording file into m_events.
    void LoadRecording(const std::string& aPath) noexcept;

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
    uint32_t GetModCount() const noexcept override;
    std::string GetModNameByIndex(uint32_t aIndex) const noexcept override;
    uint32_t GetModStandardId(const std::string& aModName) const noexcept override;
    std::vector<std::string> GetAllModNames() const noexcept override;
    uint32_t GetModFormIdBySpellId(uint32_t aModStandardId, uint32_t aSpellLocalId) const noexcept override;
    bool IsSpellItem(uint32_t aFormId) const noexcept override;
    bool IsConcentrationSpell(uint32_t aFormId) const noexcept override;
    bool IsHealingSpell(uint32_t aFormId) const noexcept override;
    bool IsWardSpell(uint32_t aFormId) const noexcept override;
    bool IsInvisibilitySpell(uint32_t aFormId) const noexcept override;
    bool IsMagicItem(uint32_t aFormId) const noexcept override;

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
    // -----------------------------------------------------------------
    uint32_t GetCurrentWeatherId() const noexcept override;
    uint32_t GetLastWeatherId() const noexcept override;
    void ForceWeather(uint32_t aWeatherFormId, bool aOverride) noexcept override;
    void ReleaseWeatherOverride() noexcept override;
    std::string GetModName(uint32_t aIndex) const noexcept override;
    void SetGameWeatherSystemEnabled(bool aEnable) noexcept override;
    float GetGameTimeHours() const noexcept override;
    void SetGameTimeHours(float aHours) noexcept override;

private:
    /// Stored event lines parsed from NDJSON recording.
    std::vector<std::string> m_eventLines;
    mutable size_t m_cursor = 0;

    /// Registered callbacks for event injection.
    std::function<void(uint32_t, bool)> m_questStartStopCb;
    std::function<void(uint32_t, uint32_t)> m_questStageCb;
    std::function<void(uint32_t, uint32_t)> m_activateCb;
    std::function<void(uint32_t, bool)> m_lockChangeCb;
};
