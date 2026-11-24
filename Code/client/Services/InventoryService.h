#pragma once

#include <Events/Listen.h>
#include <NoCopyMove.h>

#include <chrono>
#include <optional>
#include <map>
#include <set>
#include <string>
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
struct ItemsChangedEvent;
struct ReferenceLoadedEvent;
struct WornResetEvent;
struct DisconnectedEvent;
struct CopyRecords;

/**
 * @brief Actors' items as their owner's record part (docs/design/one-simulator.md, piece 3): an owner declares what its
 * actor carries and wears when it changes (kItems), a copy is made to carry and wear that. Containers' items are world
 * state (HostWorldState, GuestWorldState); the party's gold and party items are the host's (HostPartyItems).
 */
struct InventoryService
{
    InventoryService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport,
                     IActorBridge& aActorBridge, IFormBridge& aFormBridge, CopyRecords& aRecords) noexcept;
    ~InventoryService() noexcept;

    NO_COPY_MOVE(InventoryService);

    void OnUpdate(const UpdateEvent& acUpdateEvent) noexcept;
    // This game's player's item change: to the host for the party's gold and items (HostPartyItems).
    void OnInventoryChangeEvent(const InventoryChangeEvent& acEvent) noexcept;

    // The host's change to this game's own actor (a loot it granted), or a drop.
    void OnNotifyInventoryChanges(const NotifyInventoryChanges& acMessage) noexcept;
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
    void RenameAsParty(uint32_t aHolder, const Inventory::Entry& acFound, const std::string& acTagged) const noexcept;
    [[nodiscard]] std::vector<Inventory::Entry> PartyItemsOf(uint32_t aActorFormId) const noexcept;
    [[nodiscard]] std::string ItemName(const GameId& acItem) const noexcept;
    [[nodiscard]] uint8_t ItemFormType(const GameId& acItem) const noexcept;


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

    // What an actor carries and wears as its items part: no leveled lists (a base container's, not items), and for a
    // player's character not the party's gold and items.
    [[nodiscard]] Inventory ReadItems(uint32_t aFormId) const noexcept;

private:
    // An actor whose items may have changed: read again if this game owns it, its part applied again if a copy's 3D loaded.
    void Touch(uint32_t aFormId) noexcept;
    void OnEquipmentChangeEvent(const EquipmentChangeEvent& acEvent) noexcept;
    void OnItemsChanged(const ItemsChangedEvent& acEvent) noexcept;
    void OnReferenceLoaded(const ReferenceLoadedEvent& acEvent) noexcept;
    void OnWornReset(const WornResetEvent& acEvent) noexcept;
    // The engine changed what aFormId wears by itself: an owner reads its items again, a copy wears its owner's again.
    void Refresh(uint32_t aFormId) noexcept;
    // Each touched actor this game owns: its change declared.
    void DeclareChanges() noexcept;
    // The items part of aId on its copy (once its 3D is in).
    void ApplyItems(uint32_t aId, const std::string& acBytes) noexcept;
    // The part's worn items and hand spells on the copy (EquipmentRepair). False: not settled (an equip refused, a hand
    // its process does not fill yet).
    bool ApplyWorn(uint32_t aFormId, const Inventory& acWanted) noexcept;
    // Copies whose worn set did not settle: seconds since, tried again every kRetry until kGiveUp (as piece 2's hands).
    static constexpr double kRetry = 0.25, kGiveUp = 5.0;
    // A copy's spawn items are on it now (CharacterService): a player's carries the party's gold and items, not its spawn's.
    void OnSpawnItemsApplied(entt::registry& aRegistry, entt::entity aEntity) noexcept;
    void OnDisconnected(const DisconnectedEvent&) noexcept
    {
        m_partyGold.reset();
        m_partyItems.reset();
        m_touched.clear();
        m_unsettled.clear();
        m_waiting3D.clear();
    }
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
    CopyRecords& m_records;
    std::set<uint32_t> m_touched; // form ids
    std::map<uint32_t, std::pair<double, double>> m_unsettled; // session id: seconds since the first try, since the last
    std::set<uint32_t> m_waiting3D; // copies (session ids) whose part waits for their 3D (a move to another cell)

    std::chrono::steady_clock::time_point m_lastNakedCheck;
    std::optional<uint32_t> m_partyGold;
    std::optional<std::vector<Inventory::Entry>> m_partyItems;

    std::vector<entt::scoped_connection> m_connections;
};
