#pragma once
#include <Events/Listen.h>
#include <glm/vec3.hpp>
#include "Structs/Inventory.h"
#include "Structs/ActorData.h"
#include <Messages/AnnounceCharacterRequest.h>
#include <Messages/NotifyCharacterSpawn.h>
#include <Messages/NotifyRespawn.h>
#include <Structs/ActorWhereabouts.h>
#include <NoCopyMove.h>
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
struct NotifyFactionsChanges;
struct FactionsChangeEvent;
struct ReferenceJumpEvent;
struct ReferenceLoadedEvent;
struct NotifyRemoveCharacter;
struct PlayerArrivedEvent;
struct SpellCastEvent;
struct InterruptCastEvent;
struct AddTargetEvent;
struct ProjectileLaunchedEvent;
struct MountEvent;
struct DismountEvent;
struct NotifyRespawn;
struct PlayerLooksChangeEvent;
struct PlayerRevivedEvent;
struct DialogueEvent;
struct SubtitleEvent;

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
    void OnReferencesMoveRequest(const NotifyReferencesMove& acMessage) const noexcept;
    void OnActionEvent(const ActionEvent& acActionEvent) const noexcept;
    void OnFactionsChanges(const NotifyFactionsChanges& acEvent) const noexcept;
    // An owned actor's factions changed: they reach its copies if they differ from the last sent.
    void OnFactionsChange(const FactionsChangeEvent& acEvent) const noexcept;
    // An actor of ours put somewhere at once: its next snapshot says so (Movement::Teleported).
    void OnReferenceJump(const ReferenceJumpEvent& acEvent) noexcept;
    // A spawn waiting for its reference to finish loading: kStillLoading clears before its 3D loads.
    void OnReferenceLoaded(const ReferenceLoadedEvent& acEvent) noexcept;
    void OnRemoveCharacter(const NotifyRemoveCharacter& acMessage) noexcept;
    void OnMountEvent(const MountEvent& acEvent) noexcept;
    // The player's owned horse accompanies a tether move.
    void CarryMountTo(uint32_t aCellId, const glm::vec3& acPosition) noexcept;
    void OnNotifyMount(const NotifyMount& acMessage) noexcept;
    // Our player got off the mount the host gave this game: the host takes it back.
    void OnDismountEvent(const DismountEvent& acEvent) noexcept;
    // The mount is the host's again: a copy in its rider's game, the host's game's to simulate.
    void OnNotifyDismount(const NotifyDismount& acMessage) noexcept;
    void OnNotifyRespawn(const NotifyRespawn& acMessage) noexcept;
    void QueuePendingLooks() noexcept;
    void ApplyPendingLooks() noexcept;
    void OnPlayerLevel(const NotifyPlayerLevel& acMessage) const noexcept;
    // A player's copy at its owner's level (SessionService's roster).
    void SetCopyLevel(uint32_t aFormId, uint32_t aPlayerId) const noexcept;
    // Its base, build and tints reach the existing copies through the host.
    void OnPlayerLooksChange(const PlayerLooksChangeEvent& acEvent) const noexcept;
    // Its copies on the other games are made again, alive (a resurrected copy's graph stays dead).
    void OnPlayerRevived(const PlayerRevivedEvent&) const noexcept;
    void OnDialogueEvent(const DialogueEvent& acEvent) noexcept;
    void OnNotifyDialogue(const NotifyDialogue& acMessage) noexcept;
    void OnSubtitleEvent(const SubtitleEvent& acEvent) noexcept;
    void OnNotifySubtitle(const NotifySubtitle& acMessage) noexcept;
    // The player arrived somewhere new (TravelService): drops the copies the old cell took and tells the host.
    void OnPlayerArrived(const PlayerArrivedEvent& acEvent) noexcept;

    void ProcessNewEntity(entt::entity aEntity) const noexcept;

    /**
     * @brief Hosting: spawns for our own actors as the engine shows them now, but aExcludedSessionId.
     */
    std::vector<NotifyCharacterSpawn> BuildSpawnRequests(uint32_t aExcludedSessionId) const noexcept;
    /**
     * @brief Hosting: our copy of another player's character as it is now (position, items, hands, action) into
     * aSpawn; its values stay its owner's. False when we have no copy of it.
     */
    bool ReadCopy(uint32_t aSessionId, NotifyCharacterSpawn& aSpawn) const noexcept;

private:
    // The host gave this game a copy's actor (a mount its player got on): ours to simulate from here on.
    bool TakeOwnership(const uint32_t acFormId, const uint32_t acSessionId, const entt::entity acEntity) const noexcept;
    // Another game simulates this game's actor from here on (the mount, given to its rider's game or back): a copy.
    void GiveUpOwnership(entt::entity aEntity, uint32_t aSessionId, uint32_t aFormId) const noexcept;
    void MoveActor(uint32_t aFormId, const GameId& acWorldSpaceId, const GameId& acCellId, const Vector3_NetQuantize& acPosition) const noexcept;

    // One of this game's actors under the id the host gave it: this game's from here on.
    void AnnounceCharacter(entt::entity aEntity) const noexcept;
    // The actor left this game's simulation: a copy goes or is held, our own one's copies are removed.
    void ReleaseCharacter(entt::entity aEntity, uint32_t aFormId) const noexcept;
    void DeleteRemoteEntityComponents(entt::entity aEntity) const noexcept;
    // Where our actor is now, for its copies to follow when we stop simulating it.
    [[nodiscard]] ActorWhereabouts ReadWhereabouts(uint32_t aFormId) const noexcept;
    // One of this game's references whose owner's actor went: moved or disabled as it was, still a copy. True
    // when it was deleted.
    bool FollowOwner(entt::entity aEntity, uint32_t aFormId, const ActorWhereabouts& acWhereabouts) const noexcept;

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

    void RunLocalUpdates() noexcept;
    void RunRemoteUpdates() noexcept;
    void RunActorDiscovery() noexcept;
    // Copies whose owner is around us but which are elsewhere or without 3D go where the owner is.
    void RunPresence() noexcept;
    // A spawn for a copy we already have: its owner's latest place and spawn data.
    void RefreshCopy(entt::entity aEntity, const NotifyCharacterSpawn& acMessage) const noexcept;
    bool ApplyPlacedBase(uint32_t aFormId, const NotifyCharacterSpawn& acMessage) const noexcept;
    // Drops the copy and asks the host to spawn it again.
    void RespawnCopy(entt::entity aEntity, uint32_t aSessionId) const noexcept;
    // Drops our entities for the copies acShouldDrop picks (by form id).
    void DropCopies(const std::function<bool(uint32_t)>& acShouldDrop) noexcept;
    void KeepMount(uint32_t aFormId) noexcept;
    void ReleaseMount(uint32_t aFormId) noexcept;

    World& m_world;
    entt::dispatcher& m_dispatcher;
    TransportService& m_transport;
    IActorBridge& m_actorBridge;
    IFormBridge& m_formBridge;
    IPlayerBridge& m_playerBridge;
    std::unordered_set<uint32_t> m_discoveredActors;
    std::unordered_map<uint32_t, NotifyRespawn> m_pendingLooks;
    std::unordered_map<uint32_t, NotifyCharacterSpawn> m_pendingSpawns;
    std::shared_ptr<std::atomic<bool>> m_alive{std::make_shared<std::atomic<bool>>(true)};
    bool m_looksQueued{};
    // This game's player in the session (ConnectedEvent): its player id and its character's session id.
    uint32_t m_playerId{};
    uint32_t m_characterId{};
    // The mount our player rides, given to this game by the host (its session id), or 0.
    uint32_t m_riddenMount{};
    std::unordered_map<uint32_t, bool> m_mountPersistence; // form id -> persistence before this ride
    std::unordered_set<uint32_t> m_partyMounts; // session ids: ridden by a player, kept through cell loads
    std::unordered_set<uint32_t> m_jumped; // form ids, until their next snapshot
    // Ours lying in their ragdolls at the last snapshot, and the snapshots since they lay down or got up: the first two
    // are jumps (the engine moves it onto or off its ragdoll in its graph update, within a frame of the state).
    std::chrono::steady_clock::time_point m_lastSnapshot; // our actors' last movement snapshot sent

    std::vector<entt::scoped_connection> m_connections;
};
