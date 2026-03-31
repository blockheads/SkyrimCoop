#pragma once

#include <cstdint>
#include <string>
#include <vector>

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

    // --- Show HUD message ---
    virtual void ShowHudMessage(const std::string& aMessage) noexcept = 0;
};
