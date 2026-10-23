#pragma once

// One game's engine in tier 2 (docs/design/testing.md): the player's controls (ControlMap's bitmask), a script VM
// running Game.DisablePlayerControls / EnablePlayerControls through what PartyNatives' glue does around them, the
// world's references (locks, containers' items) with the change records the engine keeps, and the actors (the
// player, placed and placeatme NPCs, copies: where, health, death, bleedout, enabled).
#include "StillBridges.h"

#include <Messages/CellStateRequest.h>
#include <Messages/WorldState.h>
#include <Papyrus/PartyRules.h>
#include <Structs/ScriptValue.h>

#include <glm/glm.hpp>

#include <deque>
#include <map>
#include <optional>
#include <set>
#include <utility>
#include <vector>

struct World;

struct FakeEngine : StillBridges
{
    static constexpr uint32_t kAllControls = 0xFFFFFFFF;

    uint32_t Controls = kAllControls; // ControlMap::enabledControls
    float ShakeSeconds = 0.f;         // the camera's shake left (Game.ShakeCamera), a frame a 60th of a second
    PartyRules::ControlHolds Holds;   // PartyNatives' holds on this game
    bool InSession = false;

    // A script's call on this game, run now in a stack whose cause is aCause (GuestScripts': 0 for no player's action).
    void RunScript(const LocalCall& acCall, uint32_t aCause = 0, bool aStory = false);
    // A call the VM runs on its next frame, its stack tagged aCause (GlobalCall::QueueCall).
    void Queue(LocalCall aCall, uint32_t aCause);
    void RunQueued();
    // One frame: the shake wears off, the VM runs what is queued.
    void Frame();

    // A world reference (every game has the same load order: a form id is the same everywhere).
    struct Reference
    {
        uint32_t Cell = 0;
        bool Locked = false;
        uint8_t LockLevel = 0;
        std::map<uint32_t, int32_t> Items; // a container's count of each item
        bool operator==(const Reference&) const = default;
    };
    std::map<uint32_t, Reference> References;
    std::set<uint32_t> Attached;       // the cells loaded here
    std::set<uint32_t> InMemory;       // the cells loaded here once: their references stay in memory
    std::set<uint32_t> Changed;        // the references this game's change records hold
    std::vector<ObjectChange> Recorded; // the host's: its world's changes since HostGame::TakeObjectChanges
    bool IsGuest = false;              // a guest's engine raises its player's changes as events (GuestWorldState)
    bool DecidesWorld = true;          // GuestEngine::SetDecidesWorld

    void LoadCell(uint32_t aCell);
    void UnloadCell(uint32_t aCell);
    // This game's player picks a lock or takes from a container, as the engine's hooks report it: the host's game
    // records it, a guest's raises LockChangeEvent / ContainerChangeEvent.
    void PlayerSetsLock(uint32_t aReference, bool aLocked, uint8_t aLevel);
    void PlayerSetsItems(uint32_t aContainer, uint32_t aItem, int32_t aCount);
    // The host's game takes a change (a guest's proposal accepted, HostGame::SetLock / SetItems): made and recorded.
    void SetLock(uint32_t aReference, bool aLocked, uint8_t aLevel);
    void SetItems(uint32_t aContainer, uint32_t aItem, int32_t aCount);
    // The rows of a reference's state (ReferenceStates' encoding: a lock's level, kLocked above it; items' counts).
    [[nodiscard]] std::vector<ObjectChange> RowsOf(uint32_t aReference) const;
    // Another game's change made here (GuestEngine::ApplyObjectChange); false: no such reference.
    bool Apply(const ObjectChange& acChange, bool aLoad);
    // The host's state of a cell (HostGame::ReadCellState): the rows of the references it changed or aChanged names.
    [[nodiscard]] std::optional<std::vector<ObjectChange>> CellState(const GameId& acCell, const std::vector<ChangedReference>& acChanged) const;
    // A guest asking for a loaded cell's state (GuestEngine::ReadCellChanges).
    [[nodiscard]] std::optional<CellStateRequest> CellChanges(uint32_t aCell) const;

    // Every game's plugins, in one load order: Skyrim.esm, Update.esm, a light ccBGSSSE001-Fish.esm.
    std::vector<ModEntry> GetLoadedMods() const noexcept override
    {
        return {{0, false, "Skyrim.esm"}, {1, false, "Update.esm"}, {0, true, "ccBGSSSE001-Fish.esm"}};
    }
    // One load order: every form is in every game.
    bool FormExists(uint32_t aFormId) const noexcept override { return aFormId != 0; }

    // ActorValueInfo's health, magicka and stamina; the fake keeps no other values.
    static constexpr uint32_t kHealth = 24;
    static constexpr uint32_t kMagicka = 25;
    static constexpr uint32_t kStamina = 26;
    // Health, magicka and stamina at 100 of 100.
    [[nodiscard]] static ActorValues FullValues();

    // The engine's actors: this game's player (0x14), the NPCs its cells loaded, the copies its services made.
    struct Actor
    {
        uint32_t Base = 0;
        uint32_t Cell = 0;
        uint32_t WorldSpace = 0;
        float Position[3]{};
        float Rotation[3]{};
        bool Dead = false;
        bool Bleeding = false; // essential and down (ActorState's bleedout)
        bool Disabled = false;
        bool Deleted = false;    // marked deleted (DisableActor's aDelete)
        bool Created = false;    // made by Actor::Create (a copy): deleted with its cell unless persistent
        bool Persistent = false; // kept when its cell unloads (SetActorPersistent)
        bool Essential = false;  // goes down at 0 health instead of dying (SetPlayerRespawnMode)
        bool Copy = false;   // another game owns it (SetActorCopy)
        bool Player = false; // a player's character, this game's or a copy (SetActorPlayer)
        bool Loaded = true;  // its 3D is loaded (in the process lists)
        ActorValues Values = FullValues();
        Inventory Items;
        uint32_t Made = 0; // which actor at this form id: a reused id is a new one (a fresh ActorExtension)
    };
    std::map<uint32_t, Actor> Actors{{kPlayer, Actor{.Base = 0x7, .Player = true}}};
    static constexpr uint32_t kPlayer = 0x14;

    // This game's player travels by itself (a door, coc): to aCell at aPosition, that cell loaded and the one it
    // left unloaded, a teleport the engine reports once (ConsumeTeleported).
    void PlayerTravels(uint32_t aCell, const glm::vec3& acPosition);
    bool Teleported = false;
    float Heading = 0.f; // the player's (SetPlayerHeading)
    // Each interior's COC marker (TESObjectCELL::GetCOCPlacementInfo), where a respawn puts the player.
    std::map<uint32_t, glm::vec3> CocMarkers;

    // An NPC this game's cell loads (as `placeatme`: a temporary reference of aBase); its form id.
    uint32_t LoadNpc(uint32_t aBase, uint32_t aCell, const glm::vec3& acPosition = {});
    // An NPC placed in the plugins (the same form id in every game), in aCell at acPosition.
    void PlaceNpc(uint32_t aFormId, uint32_t aBase, uint32_t aCell, const glm::vec3& acPosition);
    // A hit of aDamage on an actor, as the engine's: at 0 health it dies, or goes down if essential; a copy is in
    // deferred kill and does neither (actor-ai-and-copies.md, "A copy is in deferred kill").
    void Hit(uint32_t aFormId, float aDamage);
    // How many actors of each NPC base this game has made (SpawnActorFromNpc), ever.
    std::map<uint32_t, size_t> Spawned;
    // The actors that are copies of aBase here (0 for any base), by form id.
    [[nodiscard]] std::vector<uint32_t> CopiesOf(uint32_t aBase = 0) const;

    // IActorBridge, IPlayerBridge: the actor model's.
    std::vector<uint32_t> GetLoadedActorFormIds() const noexcept override;
    bool ActorHas3D(uint32_t aFormId) const noexcept override;
    bool ActorExists(uint32_t aFormId) const noexcept override { return Actors.contains(aFormId); }
    void SetActorCopy(uint32_t aFormId, bool aCopy) noexcept override;
    bool IsActorCopy(uint32_t aFormId) const noexcept override;
    void SetActorPlayer(uint32_t aFormId, bool aPlayer) noexcept override;
    uint32_t GetActorBaseFormId(uint32_t aFormId) const noexcept override;
    uint32_t GetActorParentCellFormId(uint32_t aFormId) const noexcept override;
    uint32_t GetActorWorldSpaceFormId(uint32_t aFormId) const noexcept override;
    void GetActorPosition(uint32_t aFormId, float& aX, float& aY, float& aZ) const noexcept override;
    void GetActorRotation(uint32_t aFormId, float& aX, float& aY, float& aZ) const noexcept override;
    bool IsActorDead(uint32_t aFormId) const noexcept override;
    void KillActor(uint32_t aFormId) noexcept override;
    void RespawnActor(uint32_t aFormId) noexcept override;
    void ResurrectActor(uint32_t aFormId) noexcept override;
    bool DamageActorHealth(uint32_t aFormId, uint32_t aAttackerFormId, float aDamage) noexcept override;
    bool IsBleedingOut(uint32_t aFormId) const noexcept override;
    bool IsActorBleedingOut(uint32_t aFormId) const noexcept override { return IsBleedingOut(aFormId); }
    bool IsOtherPlayer(uint32_t aFormId) const noexcept override;
    bool IsTemporaryActor(uint32_t aFormId) const noexcept override { return IsActorTemporary(aFormId); }
    void SetActorPersistent(uint32_t aFormId) noexcept override;
    void SetActorPlayerRespawnMode(uint32_t aFormId) noexcept override;
    bool ApplyRemoteMovement(uint32_t aFormId, const float aPosition[3], const float aRotation[3], float aDirection,
                             const AnimationVariables& aVariables) noexcept override;
    float GetActorValue(uint32_t aFormId, int32_t aIndex) const noexcept override;
    float GetActorPermanentValue(uint32_t aFormId, int32_t aIndex) const noexcept override;
    void SetActorValue(uint32_t aFormId, int32_t aIndex, float aValue) noexcept override;
    void ForceActorValue(uint32_t aFormId, int32_t aForceMode, int32_t aIndex, float aValue) noexcept override;
    int32_t GetActorValueCount() const noexcept override { return 164; } // ActorValueInfo::kActorValueCount
    bool IsActorDisabled(uint32_t aFormId) const noexcept override;
    bool IsActorDeleted(uint32_t aFormId) const noexcept override;
    void EnableActor(uint32_t aFormId) noexcept override;
    void DisableActor(uint32_t aFormId, bool aDelete) noexcept override;
    uint32_t SpawnActorFromNpc(uint32_t aNpcFormId) noexcept override;
    uint32_t CreateCustomActor(const std::string& acAppearance, uint32_t aChangeFlags) noexcept override;
    void MoveActorTo(uint32_t aFormId, uint32_t aCellId, float aX, float aY, float aZ) noexcept override;
    void SetActorRotation(uint32_t aFormId, float aX, float aZ) noexcept override;
    void DeleteActor(uint32_t aFormId) noexcept override;
    bool IsActorTemporary(uint32_t aFormId) const noexcept override { return aFormId >= 0xFF000000; }
    bool IsActorNpcTemporary(uint32_t aFormId) const noexcept override;
    Inventory GetActorInventory(uint32_t aFormId) const noexcept override;
    void SetActorInventory(uint32_t aFormId, const Inventory& acInventory) noexcept override;
    ActorValues GetActorEssentialValues(uint32_t aFormId) const noexcept override;
    void SetActorValues(uint32_t aFormId, const ActorValues& acValues) noexcept override;
    uint32_t GetPlayerFormId() const noexcept override { return kPlayer; }
    uint32_t GetPlayerParentCellId() const noexcept override { return Actors.at(kPlayer).Cell; }
    uint32_t GetPlayerParentCellFormId() const noexcept override { return Actors.at(kPlayer).Cell; }
    uint32_t GetPlayerWorldSpaceId() const noexcept override { return Actors.at(kPlayer).WorldSpace; }
    bool IsPlayerLoaded() const noexcept override { return true; }
    void GetPlayerPosition(float& aX, float& aY, float& aZ) const noexcept override { GetActorPosition(kPlayer, aX, aY, aZ); }
    bool ConsumeTeleported() noexcept override { return std::exchange(Teleported, false); }
    float GetPlayerHeading() const noexcept override { return Heading; }
    void SetPlayerHeading(float aHeading) noexcept override { Heading = aHeading; }
    bool IsPlayerAlive() const noexcept override { return !Actors.at(kPlayer).Dead; }
    bool IsPlayerBleedingOut() const noexcept override { return Actors.at(kPlayer).Bleeding; }
    float GetPlayerActorValue(int32_t aIndex) const noexcept override { return GetActorValue(kPlayer, aIndex); }
    void ForcePlayerActorValue(int32_t aForceMode, int32_t aIndex, float aValue) noexcept override
    {
        ForceActorValue(kPlayer, aForceMode, aIndex, aValue);
    }
    void SetPlayerRespawnMode(bool aEnable) noexcept override { Actors.at(kPlayer).Essential = aEnable; }
    void RespawnPlayer() noexcept override;

    [[nodiscard]] bool Has(uint32_t aFlags) const noexcept
    {
        return (Controls & aFlags) == aFlags;
    }

    // The engine of the game World::Get() is now.
    static FakeEngine& Current() noexcept;

  private:
    // A new temporary form id: the lowest a deleted reference gave back, as the engine reuses them, else the next.
    uint32_t NewTemporary() noexcept;
    // At 0 health: dead, or down if essential; a copy is in deferred kill.
    void CheckHealth(Actor& aActor) noexcept;

    std::deque<std::pair<LocalCall, uint32_t>> m_queued;
    uint32_t m_nextTemporary = 0xFF000800;
    uint32_t m_made = 0;
    std::set<uint32_t> m_freedTemporary;
};

// Game.DisablePlayerControls / EnablePlayerControls with Papyrus' defaults for the rest: movement, fighting, camera
// switch, looking, sneaking, menu, activate, journal tabs, POV type.
LocalCall DisablePlayerControls(bool aMovement = true, bool aFighting = true, bool aCamSwitch = false,
                                bool aLooking = false, bool aSneaking = false, bool aMenu = true,
                                bool aActivate = true);
LocalCall EnablePlayerControls(bool aMovement = true, bool aFighting = true, bool aCamSwitch = true,
                               bool aLooking = true, bool aSneaking = true, bool aMenu = true, bool aActivate = true);

// Session ids of this game's form ids, one load order (ModSystem's fake).
[[nodiscard]] GameId SessionIdOf(uint32_t aFormId) noexcept;
[[nodiscard]] uint32_t FormIdOf(const GameId& acId) noexcept;

// The process holds several games: World::Get() is this one while it runs (Party::As).
void SetCurrentWorld(World* apWorld) noexcept;
