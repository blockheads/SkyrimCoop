#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <Structs/Tints.h>
#include <Structs/Inventory.h>
#include <Structs/QuestLog.h>
#include <Structs/ActionEvent.h>

/// PlayerCharacter::Get() and all pPlayer->* operations.
/// No SKSE types in any signature.
class IPlayerBridge
{
public:
    virtual ~IPlayerBridge() = default;

    // --- Identity ---
    virtual uint32_t GetPlayerFormId() const noexcept = 0;
    virtual uint32_t GetPlayerBaseNpcFormId() const noexcept = 0;

    // --- Position / world ---
    virtual void GetPlayerPosition(float& aX, float& aY, float& aZ) const noexcept = 0;
    virtual uint32_t GetPlayerCellId() const noexcept = 0;
    virtual uint32_t GetPlayerParentCellId() const noexcept = 0;
    virtual uint32_t GetPlayerWorldSpaceId() const noexcept = 0;
    virtual void SetPlayerWaypoint(float aX, float aY, float aZ, uint32_t aWorldSpaceFormId) noexcept = 0;
    virtual void RemovePlayerWaypoint() noexcept = 0;
    virtual void MovePlayerTo(uint32_t aCellId, float aX, float aY, float aZ) noexcept = 0;

    // --- Location / discord presence ---
    /// Returns the display name of the player's current location form (e.g. "Riverwood").
    /// Returns an empty string if no location is available.
    virtual std::string GetPlayerLocationName() const noexcept = 0;
    /// Returns the full name of the player's current worldspace (e.g. "Tamriel").
    /// Returns an empty string if in an interior with no worldspace.
    virtual std::string GetPlayerWorldSpaceName() const noexcept = 0;

    // --- State ---
    virtual bool IsPlayerAlive() const noexcept = 0;
    virtual bool IsPlayerBleedingOut() const noexcept = 0;
    virtual bool IsPlayerInGodMode() const noexcept = 0;
    virtual void SetPlayerGodMode(bool aEnable) noexcept = 0;
    virtual void SetPlayerRespawnMode(bool aEnable) noexcept = 0;
    virtual void RespawnPlayer() noexcept = 0;
    virtual void KnockdownPlayer(float aForce) noexcept = 0;
    virtual void FadeScreen(bool aFadeOut, bool aToBlack, float aFadeDuration,
                             bool aWithSound, float aDelay) noexcept = 0;

    // --- Economy ---
    virtual void PlayerPayGold(uint32_t aAmount) noexcept = 0;
    virtual void PlayerPayCrimeGoldToAllFactions() noexcept = 0;

    // --- Difficulty / settings ---
    virtual uint32_t GetDifficulty() const noexcept = 0;
    virtual void SetDifficulty(uint32_t aDifficulty) noexcept = 0;
    virtual float GetGreetDistance() const noexcept = 0;
    virtual void SetGreetDistance(float aDistance) noexcept = 0;

    // --- Level ---
    virtual uint16_t GetPlayerLevel() const noexcept = 0;

    // --- Actor values (player convenience accessors) ---
    virtual float GetPlayerActorValue(int32_t aIndex) const noexcept = 0;
    virtual void ForcePlayerActorValue(int32_t aForceMode, int32_t aIndex, float aValue) noexcept = 0;

    // --- Equipment / spells ---
    virtual uint32_t GetPlayerActiveMagicItemFormId(uint32_t aSlot) const noexcept = 0;
    virtual uint32_t GetPlayerEquippedShoutFormId() const noexcept = 0;
    virtual void EquipSpell(uint32_t aSpellFormId, uint32_t aSlot) noexcept = 0;
    virtual void EquipShout(uint32_t aShoutFormId) noexcept = 0;

    // --- Race ---
    virtual uint32_t GetPlayerRaceFormId() const noexcept = 0;

    // --- Objectives ---
    virtual std::vector<uint32_t> GetPlayerActiveQuestFormIds() const noexcept = 0;

    // --- 3D loaded ---
    virtual bool IsPlayerLoaded() const noexcept = 0;
    // Whether the engine teleported the player since the last call (experimental: coc only so far).
    virtual bool ConsumeTeleported() noexcept = 0;
    // Stops listening for teleports (CharacterService going away: the engine must not call into a
    // DLL that is being unloaded).
    virtual void StopTeleportEvents() noexcept = 0;

    // --- Show HUD message ---
    virtual void ShowHudMessage(const std::string& aMessage) noexcept = 0;

    // --- Quest / objectives ---
    virtual QuestLog GetPlayerQuestLog() const noexcept = 0;
    virtual Tints GetPlayerFaceTints() const noexcept = 0;

    // --- Experience ---
    virtual void AddPlayerSkillExperience(int32_t aSkillId, float aExperience) noexcept = 0;
    virtual int32_t GetPlayerLastUsedCombatSkill() const noexcept = 0;

    // --- Player parent cell for actor MoveTo ---
    virtual uint32_t GetPlayerParentCellFormId() const noexcept = 0;

    // --- BleedingOut ---
    virtual bool IsActorBleedingOut(uint32_t aFormId) const noexcept = 0;
};
