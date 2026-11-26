#pragma once
#include <core/Listen.h>
#include <glm/vec3.hpp>
#include "Structs/Inventory.h"
#include "Structs/ActorData.h"
#include <Messages/AnnounceCharacterRequest.h>
#include <Messages/NotifyCharacterSpawn.h>
#include <Messages/NotifyRespawn.h>
#include <Structs/ActorWhereabouts.h>
#include <NoCopyMove.h>
#include <core/MainLoopTasks.h>
#include <atomic>
#include <memory>
#include <Messages/RelayedMessages.h>

struct ActorAddedEvent;
struct ActorRemovedEvent;
struct UpdateEvent;
struct ConnectedEvent;
struct DisconnectedEvent;
struct EquipmentChangeEvent;
struct FormIdComponent;
struct ActionEvent;
struct NotifyCharacterSpawn;
struct NotifyPlayerLevel;
struct NotifyReferencesMove;
struct NotifyInventoryChanges;
struct ReferenceJumpEvent;
struct ReferenceLoadedEvent;
struct NpcHeldEvent;
struct NotifyRemoveCharacter;
struct PlayerArrivedEvent;
struct SpellCastEvent;
struct InterruptCastEvent;
struct AddTargetEvent;
struct ProjectileLaunchedEvent;
struct MountEvent;
struct BoardEvent;
struct DismountEvent;
struct NotifyRespawn;
struct PlayerLooksChangeEvent;
struct ActorHeadUpdateEvent;
struct PlayerRevivedEvent;

class IActorBridge;
class IFormBridge;
class IPlayerBridge;

struct World;
struct TransportService;

/**
 * @brief Handles actors and players.
 */
struct CharacterService
{
    CharacterService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport,
                     IActorBridge& aActorBridge, IFormBridge& aFormBridge, IPlayerBridge& aPlayerBridge) noexcept;
    ~CharacterService() noexcept;

    NO_COPY_MOVE(CharacterService);

    static void DeleteTempActor(const uint32_t aFormId) noexcept;

    void OnActorAdded(const ActorAddedEvent& acEvent) noexcept;
    void OnActorRemoved(const ActorRemovedEvent& acEvent) noexcept;
    void OnUpdate(const UpdateEvent& acUpdateEvent) noexcept;
    void OnConnected(const ConnectedEvent& acConnectedEvent) noexcept;
    void OnDisconnected(const DisconnectedEvent& acDisconnectedEvent) noexcept;
    void OnCharacterSpawn(const NotifyCharacterSpawn& acMessage) noexcept;
    // A spawn waiting for its reference to finish loading: kStillLoading clears before its 3D loads.
    void OnReferenceLoaded(const ReferenceLoadedEvent& acEvent) noexcept;
    void OnNpcHeld(const NpcHeldEvent& acEvent) noexcept;
    // The actor of that session id is on this game now: its record's parts apply (CopyRecords).
    void SessionIdHere(uint32_t aSessionId) const noexcept;
    // A spawn waiting on that actor to finish loading goes on.
    void SpawnPending(uint32_t aFormId) noexcept;
    void OnRemoveCharacter(const NotifyRemoveCharacter& acMessage) noexcept;
    // A copy's name is its record's (ActorStateService); this is the host's rename of an actor this game owns.
    void OnNotifyActorName(const NotifyActorName& acMessage) const noexcept;
    void OnNotifyRespawn(const NotifyRespawn& acMessage) noexcept;
    void QueuePendingLooks() noexcept;
    void ApplyPendingLooks() noexcept;
    void OnPlayerLevel(const NotifyPlayerLevel& acMessage) const noexcept;
    // A player's copy at its owner's level (SessionService's roster).
    void SetCopyLevel(uint32_t aFormId, uint32_t aPlayerId) const noexcept;
    // Its base, build and tints reach the existing copies through the host.
    void OnPlayerLooksChange(const PlayerLooksChangeEvent& acEvent) const noexcept;
    // A copy's face tints on its head: new tints, and each head built again (a new area, TESNPC 24724).
    void PaintFace(entt::entity aEntity, uint32_t aFormId) noexcept;
    void OnActorHeadUpdate(const ActorHeadUpdateEvent& acEvent) noexcept;
    // Its copies on the other games are made again, alive (a resurrected copy's graph stays dead).
    void OnPlayerRevived(const PlayerRevivedEvent&) const noexcept;
    // The player arrived somewhere new (TravelService): drops the copies the old cell took and tells the host.
    void OnPlayerArrived(const PlayerArrivedEvent& acEvent) noexcept;

    void ProcessNewEntity(entt::entity aEntity) const noexcept;

    // This game's player's character in the session (its session id), or 0.
    [[nodiscard]] uint32_t CharacterId() const noexcept { return m_characterId; }
    // The actor of that session id on this game (its form id), or 0.
    [[nodiscard]] uint32_t FormOfSession(uint32_t aSessionId) const noexcept;

    /**
     * @brief Hosting: spawns for our own actors as the engine shows them now, but aExcludedSessionId.
     */
    std::vector<NotifyCharacterSpawn> BuildSpawnRequests(uint32_t aExcludedSessionId) const noexcept;
    /**
     * @brief Hosting: our copy of another player's character as it is now (position, items, hands, action) into
     * aSpawn; its values stay its owner's. False when we have no copy of it.
     */
    bool ReadCopy(uint32_t aSessionId, NotifyCharacterSpawn& aSpawn) const noexcept;
    // A spawn of our copy of an NPC another game owns, read from the engine as it is now.
    std::optional<NotifyCharacterSpawn> BuildCopySpawn(uint32_t aSessionId) const noexcept;

    // The host gave this game a copy's actor (a claim: ClaimService): ours to simulate from here on. aHoldKill: the
    // copy's deferred kill stays (its death waits for the give-back); else it ends.
    bool TakeOwnership(const uint32_t acFormId, const uint32_t acSessionId, const entt::entity acEntity, bool aHoldKill = false) const noexcept;
    // Another game simulates this game's actor from here on (claimed for it, or given back): a copy.
    void GiveUpOwnership(entt::entity aEntity, uint32_t aSessionId, uint32_t aFormId) const noexcept;
    // Where our actor is now, for its copies to follow when we stop simulating it.
    [[nodiscard]] ActorWhereabouts ReadWhereabouts(uint32_t aFormId) const noexcept;
    // One of this game's references whose owner's actor went: moved or disabled as it was, still a copy. With no owner
    // left (aOwnerGone), CopyHeldEvent first: a guest hides it (GuestCopies).
    void FollowOwner(entt::entity aEntity, uint32_t aFormId, const ActorWhereabouts& acWhereabouts, bool aOwnerGone) const noexcept;
    // Our actor taken back from a claim: exactly where its last holder left it, its heading too.
    void PlaceAt(uint32_t aFormId, const ActorWhereabouts& acWhereabouts) const noexcept;
    // A copy's spawn: its owner's newest place, cell and tick, not where playback shows it.
    void PlaceFromOwner(entt::entity aEntity, NotifyCharacterSpawn& aSpawn) const noexcept;

private:
    void MoveActor(uint32_t aFormId, const GameId& acWorldSpaceId, const GameId& acCellId, const Vector3_NetQuantize& acPosition) const noexcept;

    // One of this game's actors under the id the host gave it: this game's from here on.
    void AnnounceCharacter(entt::entity aEntity) const noexcept;
    // The actor left this game's simulation: a copy goes or is held, our own one's copies are removed.
    void ReleaseCharacter(entt::entity aEntity, uint32_t aFormId) const noexcept;
    void DeleteRemoteEntityComponents(entt::entity aEntity) const noexcept;

    ActorData BuildActorData(uint32_t aFormId) const noexcept;
    // An actor as it is now: where, its values, inventory and hands, and the action it last took.
    struct ActorLiveState
    {
        Vector3_NetQuantize Position{};
        Rotator2_NetQuantize Rotation{};
        ActorData Data{};
        ActionEvent LatestAction{};
        // Its behaviour graphs' state, for a copy to start in (IActorBridge::SaveActorGraphState).
        std::string GraphState{};
    };
    ActorLiveState ReadLiveState(uint32_t aFormId) const noexcept;
    // Everything a peer needs about one of our actors, read from the engine now.
    std::optional<AnnounceCharacterRequest> BuildAnnouncement(entt::entity aEntity) const noexcept;

    void RunActorDiscovery() noexcept;
    // Copies whose owner is around us but which are elsewhere or without 3D go where the owner is.
    void RunPresence() noexcept;
    // A copy whose 3D arrived: its spawn's variables and life, then its record's parts.
    void RunCopy3D() noexcept;
    // A spawn for a copy we already have: its owner's latest place and spawn data.
    void RefreshCopy(entt::entity aEntity, const NotifyCharacterSpawn& acMessage) const noexcept;
    bool ApplyNpcSnapshot(uint32_t aFormId, const NotifyCharacterSpawn& acMessage) const noexcept;
    // Drops the copy and asks the host to spawn it again.
    void RespawnCopy(entt::entity aEntity, uint32_t aSessionId) const noexcept;
    // Drops our entities for the copies acShouldDrop picks (by form id).
    void DropCopies(const std::function<bool(uint32_t)>& acShouldDrop) noexcept;

    World& m_world;
    entt::dispatcher& m_dispatcher;
    TransportService& m_transport;
    IActorBridge& m_actorBridge;
    IFormBridge& m_formBridge;
    IPlayerBridge& m_playerBridge;
    std::unordered_set<uint32_t> m_discoveredActors;
    std::unordered_map<uint32_t, NotifyRespawn> m_pendingLooks;
    std::unordered_map<uint32_t, NotifyCharacterSpawn> m_pendingSpawns;
    MainLoopTasks::Scope m_mainTasks;
    bool m_looksQueued{};
    // This game's player in the session (ConnectedEvent): its player id and its character's session id.
    uint32_t m_playerId{};
    uint32_t m_characterId{};
    std::unordered_map<uint32_t, uint32_t> m_pendingCommanders; // a summon -> its commander, before the commander's copy
    void LinkPendingCommanders(uint32_t aSessionId) noexcept;

    std::vector<entt::scoped_connection> m_connections;
};
