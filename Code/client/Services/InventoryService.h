#pragma once

#include <Events/Listen.h>
#include <NoCopyMove.h>

#include <chrono>
#include <optional>
#include <utility>
#include <vector>
#include <Messages/RelayedMessages.h>
#include <Structs/Inventory.h>
#include <Structs/GameId.h>
#include <Games/EquipmentRepair.h>

class IActorBridge;
class IFormBridge;

struct World;
struct TransportService;

struct UpdateEvent;
struct NotifyInventoryChanges;
struct InventoryChangeEvent;
struct EquipmentChangeEvent;
struct NotifyEquipmentChanges;
struct DisconnectedEvent;

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
    /**
     * @brief The party's gold (docs/design/party-purse.md): this game's player and every copy of a player carry it.
     */
    void OnNotifyPartyGold(const NotifyPartyGold& acMessage) noexcept;
    // A party item (party-items.md): this game's player and every player copy carry the party's count of that stack.
    void OnNotifyPartyItem(const NotifyPartyItem& acMessage) noexcept;
    void SetPartyItem(uint32_t aActorFormId, const Inventory::Entry& acItem) const noexcept;
    // Every party item (a join): carried at the party's count, and a tagged stack the party lacks dropped.
    void OnNotifyPartyItems(const NotifyPartyItems& acMessage) noexcept;
    void SetPartyItems(uint32_t aActorFormId, const std::vector<Inventory::Entry>& acItems) const noexcept;

    // The host's party items (HostGame's seams): how many of that stack (its base and name) the actor carries, a
    // change to it silently, every party stack it carries, and an item's name.
    [[nodiscard]] int32_t PartyItemCount(uint32_t aActorFormId, const Inventory::Entry& acItem) const noexcept;
    void AddPartyItem(uint32_t aActorFormId, const Inventory::Entry& acItem) const noexcept;
    [[nodiscard]] std::vector<Inventory::Entry> PartyItemsOf(uint32_t aActorFormId) const noexcept;
    [[nodiscard]] std::string ItemName(const GameId& acItem) const noexcept;


    // A copy about to equip an item it does not carry gets one, silently (its owner wears it).
    void CarryForEquip(uint32_t aActorFormId, const GameId& acItem) const noexcept;
    // How much gold (Gold001) the actor carries, and setting it, silently (not sent).
    [[nodiscard]] uint32_t GoldOf(uint32_t aActorFormId) const noexcept;
    void SetGold(uint32_t aActorFormId, uint32_t aGold) const noexcept;
    // aCount more or fewer, silently; a removal takes at most what it has.
    void AddGold(uint32_t aActorFormId, int32_t aCount) const noexcept
    {
        SetGold(aActorFormId, static_cast<uint32_t>(std::max<int64_t>(int64_t{GoldOf(aActorFormId)} + aCount, 0)));
    }

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
    // A copy's spawn items are on it now (CharacterService): a player's carries the party's gold and items, not its spawn's.
    void OnSpawnItemsApplied(entt::registry& aRegistry, entt::entity aEntity) noexcept;
    void OnDisconnected(const DisconnectedEvent&) noexcept
    {
        m_partyGold.reset();
        m_partyItems.reset();
    }
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

    EquipmentRepair::Snapshots m_equipmentSnapshots;
    std::chrono::steady_clock::time_point m_lastNakedCheck;
    std::optional<uint32_t> m_partyGold;
    std::optional<std::vector<Inventory::Entry>> m_partyItems;

    std::vector<entt::scoped_connection> m_connections;
};
