#pragma once

struct NotifyGlobals;
struct NotifyQuests;
struct NotifyObjectChanges;
struct LockChangeEvent;
struct ContainerChangeEvent;
struct UpdateEvent;
struct World;
class GuestEngine;

/**
 * @brief A guest takes the host's globals and quests (HostWorldState) with its engine's own calls
 * (docs/design/world-state-sync.md step 4). Its own story manager starts no quest meanwhile, and its world
 * scripts do not react to its player: the host's world scripts' doors and animations come from the host (step 5).
 * Its own lock and container changes are proposals the host's game takes (step 6). Each cell it loads asks for the
 * host's state there, what it missed while away included.
 */
struct GuestWorldState
{
    GuestWorldState(World& aWorld, entt::dispatcher& aDispatcher, GuestEngine& aEngine) noexcept;
    ~GuestWorldState() noexcept;

    TP_NOCOPYMOVE(GuestWorldState);

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

    World& m_world;
    GuestEngine& m_engine;
    // The attached cells already asked for.
    TiltedPhoques::Vector<uint32_t> m_cells;

    entt::scoped_connection m_globalsConnection;
    entt::scoped_connection m_questsConnection;
    entt::scoped_connection m_objectChangesConnection;
    entt::scoped_connection m_lockChangeConnection;
    entt::scoped_connection m_containerChangeConnection;
};
