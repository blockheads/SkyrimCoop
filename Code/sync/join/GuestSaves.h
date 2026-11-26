#pragma once

#include <core/Listen.h>
#include <NoCopyMove.h>
#include <join/SaveTransfer.h>

#include <memory>

class IJoinBridge;
struct NotifySaveChunk;
struct World;

/**
 * @brief The saves the host sends ahead while playing (docs/design/join-flow.md, "Save sync"): each is decoded off
 * the game's threads, held in this game's store and named to the host, so a later load of it needs no transfer.
 */
struct GuestSaves
{
    GuestSaves(World& aWorld, entt::dispatcher& aDispatcher, IJoinBridge& aBridge) noexcept;

    NO_COPY_MOVE(GuestSaves);

private:
    void OnSaveChunk(const NotifySaveChunk& acChunk) noexcept;

    World& m_world;
    IJoinBridge& m_bridge;
    SaveTransfer::Assembly m_save;
    std::shared_ptr<bool> m_pAlive = std::make_shared<bool>(true); // work finishing after the session is dropped
    std::vector<entt::scoped_connection> m_connections;
};
