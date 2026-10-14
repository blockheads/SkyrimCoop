#pragma once
#include "Structs/Inventory.h"
#include "Structs/ActorData.h"
#include <Messages/AssignCharacterRequest.h>
#include <Messages/CharacterSpawnRequest.h>

struct ActorAddedEvent;
struct ActorRemovedEvent;
struct UpdateEvent;
struct ConnectedEvent;
struct DisconnectedEvent;
struct EquipmentChangeEvent;
struct FormIdComponent;
struct ActionEvent;
struct AssignCharacterResponse;
struct CharacterSpawnRequest;
struct ServerReferencesMoveRequest;
struct NotifyInventoryChanges;
struct NotifyFactionsChanges;
struct NotifyRemoveCharacter;
struct PlayerArrivedEvent;
struct NotifySpawnData;
struct SpellCastEvent;
struct NotifySpellCast;
struct InterruptCastEvent;
struct NotifyInterruptCast;
struct AddTargetEvent;
struct NotifyAddTarget;
struct ProjectileLaunchedEvent;
struct NotifyProjectileLaunch;
struct MountEvent;
struct NotifyMount;
struct InitPackageEvent;
struct NotifyNewPackage;
struct NotifyRespawn;
struct BeastFormChangeEvent;
struct AddExperienceEvent;
struct NotifySyncExperience;
struct DialogueEvent;
struct NotifyDialogue;
struct SubtitleEvent;
struct NotifySubtitle;
struct NotifyRelinquishControl;

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

    TP_NOCOPYMOVE(CharacterService);

    static void DeleteTempActor(const uint32_t aFormId) noexcept;

    bool TakeOwnership(const uint32_t acFormId, const uint32_t acServerId, const entt::entity acEntity) const noexcept;

    void OnActorAdded(const ActorAddedEvent& acEvent) noexcept;
    void OnActorRemoved(const ActorRemovedEvent& acEvent) noexcept;
    void OnUpdate(const UpdateEvent& acUpdateEvent) noexcept;
    void OnConnected(const ConnectedEvent& acConnectedEvent) const noexcept;
    void OnDisconnected(const DisconnectedEvent& acDisconnectedEvent) noexcept;
    void OnAssignCharacter(const AssignCharacterResponse& acMessage) noexcept;
    void OnCharacterSpawn(const CharacterSpawnRequest& acMessage) const noexcept;
    void OnReferencesMoveRequest(const ServerReferencesMoveRequest& acMessage) const noexcept;
    void OnActionEvent(const ActionEvent& acActionEvent) const noexcept;
    void OnFactionsChanges(const NotifyFactionsChanges& acEvent) const noexcept;
    void OnRemoveCharacter(const NotifyRemoveCharacter& acMessage) const noexcept;
    void OnRemoteSpawnDataReceived(const NotifySpawnData& acEvent) noexcept;
    void OnMountEvent(const MountEvent& acEvent) const noexcept;
    void OnNotifyMount(const NotifyMount& acMessage) const noexcept;
    void OnInitPackageEvent(const InitPackageEvent& acEvent) const noexcept;
    void OnNotifyNewPackage(const NotifyNewPackage& acMessage) const noexcept;
    void OnNotifyRespawn(const NotifyRespawn& acMessage) const noexcept;
    void OnBeastFormChange(const BeastFormChangeEvent& acEvent) const noexcept;
    void OnAddExperienceEvent(const AddExperienceEvent& acEvent) noexcept;
    void OnNotifySyncExperience(const NotifySyncExperience& acMessage) noexcept;
    void OnDialogueEvent(const DialogueEvent& acEvent) noexcept;
    void OnNotifyDialogue(const NotifyDialogue& acMessage) noexcept;
    void OnSubtitleEvent(const SubtitleEvent& acEvent) noexcept;
    void OnNotifySubtitle(const NotifySubtitle& acMessage) noexcept;
    void OnNotifyRelinquishControl(const NotifyRelinquishControl& acMessage) noexcept;
    // The player arrived somewhere new (TravelService): drops the copies the old cell took and tells the host.
    void OnPlayerArrived(const PlayerArrivedEvent& acEvent) noexcept;

    void ProcessNewEntity(entt::entity aEntity) const noexcept;

    /**
     * @brief Hosting: spawns for our own actors as the engine shows them now, but aExcludedServerId.
     */
    Vector<CharacterSpawnRequest> BuildSpawnRequests(uint32_t aExcludedServerId) const noexcept;
    /**
     * @brief Hosting: our copy of another player's character as it is now (position, values,
     * items, hands, action) into aSpawn; false when we have no copy of it.
     */
    bool ReadCopy(uint32_t aServerId, CharacterSpawnRequest& aSpawn) const noexcept;

private:
    void MoveActor(uint32_t aFormId, const GameId& acWorldSpaceId, const GameId& acCellId, const Vector3_NetQuantize& acPosition) const noexcept;

    void RequestServerAssignment(entt::entity aEntity) const noexcept;
    void CancelServerAssignment(entt::entity aEntity, uint32_t aFormId) const noexcept;
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
        String GraphState{};
    };
    ActorLiveState ReadLiveState(uint32_t aFormId) const noexcept;
    // Everything a peer needs about one of our actors, read from the engine now.
    std::optional<AssignCharacterRequest> BuildAssignment(entt::entity aEntity) const noexcept;

    void RunLocalUpdates() const noexcept;
    void RunRemoteUpdates() noexcept;
    void RunActorDiscovery() noexcept;
    void RunFactionsUpdates() const noexcept;
    void RunExperienceUpdates() noexcept;
    // Copies whose owner is around us but which are elsewhere or without 3D go where the owner is.
    void RunPresence() noexcept;
    // A spawn for a copy we already have: its owner's latest place and spawn data.
    void RefreshCopy(entt::entity aEntity, const CharacterSpawnRequest& acMessage) const noexcept;
    // Drops the copy and asks the host to spawn it again.
    void RespawnCopy(entt::entity aEntity, uint32_t aServerId) const noexcept;
    // Drops our entities for the copies acShouldDrop picks (by form id).
    void DropCopies(const std::function<bool(uint32_t)>& acShouldDrop) noexcept;

    World& m_world;
    entt::dispatcher& m_dispatcher;
    TransportService& m_transport;
    IActorBridge& m_actorBridge;
    IFormBridge& m_formBridge;
    IPlayerBridge& m_playerBridge;
    Set<uint32_t> m_discoveredActors;


    float m_cachedExperience = 0.f;


    entt::scoped_connection m_referenceAddedConnection;
    entt::scoped_connection m_referenceRemovedConnection;
    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_actionConnection;
    entt::scoped_connection m_factionsConnection;
    entt::scoped_connection m_removeCharacterConnection;
    entt::scoped_connection m_connectedConnection;
    entt::scoped_connection m_disconnectedConnection;
    entt::scoped_connection m_assignCharacterConnection;
    entt::scoped_connection m_characterSpawnConnection;
    entt::scoped_connection m_referenceMovementSnapshotConnection;
    entt::scoped_connection m_remoteSpawnDataReceivedConnection;
    entt::scoped_connection m_mountConnection;
    entt::scoped_connection m_notifyMountConnection;
    entt::scoped_connection m_initPackageConnection;
    entt::scoped_connection m_newPackageConnection;
    entt::scoped_connection m_notifyRespawnConnection;
    entt::scoped_connection m_playerArrivedConnection;
    entt::scoped_connection m_beastFormChangeConnection;
    entt::scoped_connection m_addExperienceEventConnection;
    entt::scoped_connection m_syncExperienceConnection;
    entt::scoped_connection m_dialogueEventConnection;
    entt::scoped_connection m_dialogueSyncConnection;
    entt::scoped_connection m_subtitleEventConnection;
    entt::scoped_connection m_subtitleSyncConnection;
    entt::scoped_connection m_relinquishConnection;
};
