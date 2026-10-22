#pragma once

#include <NoCopyMove.h>

class IActorBridge;
class IFormBridge;

struct World;
struct TransportService;

struct UpdateEvent;
struct NotifyInventoryChanges;
struct InventoryChangeEvent;
struct EquipmentChangeEvent;
struct NotifyEquipmentChanges;
struct NotifyEquipmentSnapshot;
struct NotifyDrawWeapon;

/**
 * @brief Manages actors' inventories.
 *
 * The initial contents of actor inventories are synced through spawn messages through CharacterService
 * @see CharacterService
 * The InventoryService manages changes to actors' equipment and inventories. Containers' items are world
 * state: the host's (HostWorldState, GuestWorldState).
 */
struct InventoryService
{
    InventoryService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport,
                     IActorBridge& aActorBridge, IFormBridge& aFormBridge) noexcept;
    ~InventoryService() noexcept = default;

    NO_COPY_MOVE(InventoryService);

    /**
     * @brief Check weapon state.
     * @see RunWeaponStateUpdates
     */
    void OnUpdate(const UpdateEvent& acUpdateEvent) noexcept;
    /**
     * @brief Sends out inventory changes made by the local client.
     */
    void OnInventoryChangeEvent(const InventoryChangeEvent& acEvent) noexcept;
    /**
     * @brief Sends out equipment changes made by the local client.
     */
    void OnEquipmentChangeEvent(const EquipmentChangeEvent& acEvent) noexcept;

    /**
     * @brief Applies inventory changes sent by the server.
     */
    void OnNotifyInventoryChanges(const NotifyInventoryChanges& acMessage) noexcept;
    /**
     * @brief Applies equipment changes sent by the server.
     */
    void OnNotifyEquipmentChanges(const NotifyEquipmentChanges& acMessage) noexcept;
    void OnNotifyEquipmentSnapshot(const NotifyEquipmentSnapshot& acMessage) noexcept;
    /**
     * @brief A copy's owner raised or lowered its hands.
     */
    void OnNotifyDrawWeapon(const NotifyDrawWeapon& acMessage) noexcept;

private:
    /**
     * Sends the hands' direction of actors this game owns when it changes (and once when an actor
     * becomes ours). The owner decides; the server only relays.
     */
    void RunWeaponStateUpdates() noexcept;
    /**
     * Copies replay their owner's draw/sheathe animations. A refused replay would leave a copy's
     * hands wrong for good: once it has nothing left to replay, it draws or sheathes to match.
     */
    void RunWeaponDrawConvergence(double aDelta) noexcept;
    void OnLocalComponentRemoved(entt::registry& aRegistry, entt::entity aEntity) noexcept;
    void OnRemoteComponentRemoved(entt::registry& aRegistry, entt::entity aEntity) noexcept;
    void RunEquipmentSnapshots() noexcept;
    /**
    * Checks whether an NPC's (local or remote) equipment is bugged (i.e. naked NPCS)
    * and resets their inventory.
    */
    void RunNakedNPCBugChecks() noexcept;

    World& m_world;
    entt::dispatcher& m_dispatcher;
    TransportService& m_transport;
    IActorBridge& m_actorBridge;
    IFormBridge& m_formBridge;

    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_inventoryConnection;
    entt::scoped_connection m_equipmentConnection;
    entt::scoped_connection m_inventoryChangeConnection;
    entt::scoped_connection m_equipmentChangeConnection;
    entt::scoped_connection m_equipmentSnapshotConnection;
    entt::scoped_connection m_drawWeaponConnection;
    entt::scoped_connection m_localComponentRemoved;
    entt::scoped_connection m_remoteComponentRemoved;
};
