#pragma once

#include <Events/Listen.h>
#include <Messages/WaitMessages.h>
#include <NoCopyMove.h>

struct World;
class GuestEngine;
struct MenuEvent;
struct GameLoadedEvent;

// Reconciles only the world snapshot requested when a shared rest's native menu closes.
struct GuestWait
{
    GuestWait(World& aWorld, entt::dispatcher& aDispatcher, GuestEngine& aEngine) noexcept;
    NO_COPY_MOVE(GuestWait);

    // After physics: only this rest's record rows wait for their placements to finish.
    void OnUpdate() noexcept;

private:
    void OnState(const NotifyWaitState& acMessage) noexcept;
    void OnMenu(const MenuEvent& acEvent) noexcept;
    void OnWorld(const NotifyWaitWorld& acMessage) noexcept;
    void OnChanges(const NotifyObjectChanges& acMessage) noexcept;
    void OnLoaded(const GameLoadedEvent&) noexcept;

    struct Record
    {
        GameId Cell;
        ObjectChange Change;
    };

    World& m_world;
    GuestEngine& m_engine;
    uint32_t m_id{};
    bool m_finished{};
    std::vector<GameId> m_cells;
    std::vector<Record> m_records;
    std::vector<entt::scoped_connection> m_connections;
};
