#pragma once

#include <Events/Listen.h>
#include <Messages/RelayedMessages.h>
#include <Structs/GameId.h>
#include <NoCopyMove.h>

class GuestEngine;
struct World;

/**
 * @brief Each player's own character in the host's engine scenes (docs/design/cutscenes.md, step 2): while the host's
 * player runs a scene's package (its AI state, AiStateService), this game's player runs that package too, AI-driven,
 * until the scene lets it go. The host's scenes cast its player alone; the party plays the part, each on its own game.
 */
struct GuestScene
{
    GuestScene(World& aWorld, entt::dispatcher& aDispatcher, GuestEngine& aEngine) noexcept;
    // Leaving mid-scene: this game's player is its own again.
    ~GuestScene() noexcept;

    NO_COPY_MOVE(GuestScene);

private:
    void OnAiState(const NotifyAiState& acMessage) noexcept;

    World& m_world;
    GuestEngine& m_engine;
    GameId m_scene{};   // the scene this game's player plays a part in, none
    GameId m_package{}; // its package now

    std::vector<entt::scoped_connection> m_connections;
};
