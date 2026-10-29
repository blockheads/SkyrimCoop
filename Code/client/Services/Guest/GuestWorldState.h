#pragma once

#include <Events/Listen.h>
#include <NoCopyMove.h>

struct NotifyGlobals;
struct NotifyQuests;
struct NotifyObjectChanges;
struct LockChangeEvent;
struct ContainerChangeEvent;
struct ItemDroppedEvent;
struct ItemPickUpEvent;
struct PlayerCraftEvent;
struct NotifyItemDropped;
struct NotifyPickUp;
struct UpdateEvent;
struct World;
class GuestEngine;

/**
 * @brief A guest takes the host's globals and quests (HostWorldState) with its engine's own calls
 * (docs/design/world-state-sync.md step 4). Its own story manager starts no quest meanwhile, and its world
 * scripts do not react to its player: the host's world scripts' doors and animations come from the host (step 5).
 * Its own lock and container changes are proposals the host's game takes (step 6). Each cell it loads asks for the
 * host's state there, what it missed while away included. Its player's crafts are the host's story events. Its
 * player's drops are made in the host's world, and its pickups of the party's loose items are the host's to grant
 * (docs/design/dropped-items.md).
 */
struct GuestWorldState
{
    GuestWorldState(World& aWorld, entt::dispatcher& aDispatcher, GuestEngine& aEngine) noexcept;
    ~GuestWorldState() noexcept;

    NO_COPY_MOVE(GuestWorldState);

    // Each frame, from GuestService: each cell newly attached here (a load or back from the cell buffer, which raises
    // no load event) asks for the host's state there.
    void OnUpdate(const UpdateEvent& acEvent) noexcept;

private:
    void OnGlobals(const NotifyGlobals& acMessage) noexcept;
    void OnQuests(const NotifyQuests& acMessage) noexcept;
    void OnObjectChanges(const NotifyObjectChanges& acMessage) noexcept;
    // This game's lock change (its player picked it, or a key): proposed to the host, whose world decides.
    void OnLockChange(const LockChangeEvent& acEvent) noexcept;
    // This game's container changed (its player took or stored an item): its count now proposed to the host.
    void OnContainerChange(const ContainerChangeEvent& acEvent) noexcept;
    void AskCellState(uint32_t aCellFormId) noexcept;
    // This game's player crafted: its story event goes to the host's story manager (HostStory), this one's starts no quest.
    void OnCraft(const PlayerCraftEvent& acEvent) noexcept;
    // This game's player dropped an item: the host's game makes the party's, this one becomes its twin.
    void OnItemDropped(const ItemDroppedEvent& acEvent) noexcept;
    void OnItemDroppedNotify(const NotifyItemDropped& acMessage) noexcept;
    // This game's player reached for the party's loose item: the host's game grants it to the first to ask.
    void OnItemPickUp(const ItemPickUpEvent& acEvent) noexcept;
    void OnPickUpNotify(const NotifyPickUp& acMessage) noexcept;

    World& m_world;
    GuestEngine& m_engine;
    // The attached cells already asked for.
    std::vector<uint32_t> m_cells;

    std::vector<entt::scoped_connection> m_connections;
};
