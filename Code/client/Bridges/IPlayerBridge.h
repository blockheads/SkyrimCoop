#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <Structs/Tints.h>
#include <Structs/Inventory.h>
#include <Structs/ActionEvent.h>

/// PlayerCharacter::Get() and all pPlayer->* operations.
/// No SKSE types in any signature.
class IPlayerBridge
{
public:
    virtual ~IPlayerBridge() = default;

    // Opens the authorized follower trade menu on this player's game.
    virtual bool OpenFollowerInventory(uint32_t aNpcFormId) noexcept = 0;

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

    // --- Location / discord presence ---
    /// Returns the display name of the player's current location form (e.g. "Riverwood").
    /// Returns an empty string if no location is available.
    virtual std::string GetPlayerLocationName() const noexcept = 0;
    /// Returns the full name of the player's current worldspace (e.g. "Tamriel").
    /// Returns an empty string if in an interior with no worldspace.
    virtual std::string GetPlayerWorldSpaceName() const noexcept = 0;

    // --- State ---
    virtual bool IsPlayerAlive() const noexcept = 0;
    virtual bool IsPlayerMounted() const noexcept = 0;
    // In a session a dead player waits for the party instead of loading the last save.
    virtual void HoldPlayerDeathReload(bool aHold) noexcept = 0;
    // Back alive from a real death (docs/design/death.md, "Real death").
    virtual void RevivePlayer() noexcept = 0;
    virtual void SetSpectatorTarget(uint32_t aFormId) noexcept = 0;
    // Each frame while spectating: the view stays third person (docs/design/death.md, "Spectating").
    virtual void KeepSpectatorView() noexcept = 0;
    virtual void HoldDeathControls(bool aHold) noexcept = 0;
    [[nodiscard]] virtual bool IsScriptCameraHeld() const noexcept = 0;

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
    // Whether the engine teleported the player since the last call (PositionPlayerEvent).
    virtual bool ConsumeTeleported() noexcept = 0;
    // The way the player faces (radians clockwise from north).
    virtual float GetPlayerHeading() const noexcept = 0;
    // Turns the player, and with it the camera, to face this way.
    virtual void SetPlayerHeading(float aHeading) noexcept = 0;
    // The players stacked on one spot, who pass through each other; everyone else collides as usual. Every frame
    // while it lasts: an actor's collision group comes from its current controller.
    virtual void SetPassThrough(const std::vector<uint32_t>& acActorFormIds) noexcept = 0;
    // Stops listening for teleports (CharacterService going away: the engine must not call into a
    // DLL that is being unloaded).
    virtual void StopTeleportEvents() noexcept = 0;

    // --- Show HUD message ---
    virtual void ShowHudMessage(const std::string& aMessage) noexcept = 0;
    // Shows that actor's health bar on the HUD, as when the player's hit lowers its health.
    virtual void ShowEnemyHealth(uint32_t aActorFormId) noexcept = 0;

    virtual Tints GetPlayerFaceTints() const noexcept = 0;


    // --- Player parent cell for actor MoveTo ---
    virtual uint32_t GetPlayerParentCellFormId() const noexcept = 0;

    // --- BleedingOut ---
    virtual bool IsActorBleedingOut(uint32_t aFormId) const noexcept = 0;
};
