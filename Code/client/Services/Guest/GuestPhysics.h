#pragma once

#include <Events/Listen.h>
#include <Messages/RelayedMessages.h>
#include <Physics/BodyPlayback.h>
#include <Services/Guest/GuestHands.h>

#include <NoCopyMove.h>

#include <set>

struct UpdateEvent;
struct World;
class GuestEngine;

/**
 * @brief A guest shows the host's bodies (docs/design/physics-sync.md, "How a guest applies it"):
 *
 *   frames   played kDelayMs behind the host's time (BodyPlayback): each body put at the pose each update, held
 *            keyframed, its node with it
 *   rest     placed exactly, then simulated again (asleep) so this game's player can push and grab it
 *
 * What this game's player pushes or holds, and what this game's own physics moved, is GuestHands' until the host's rest.
 */
struct GuestPhysics
{
    // Behind the host's time: a frame due now has arrived (half a round trip and a step's jitter).
    static constexpr uint64_t kDelayMs = 100;

    GuestPhysics(World& aWorld, entt::dispatcher& aDispatcher, GuestEngine& aEngine) noexcept;

    NO_COPY_MOVE(GuestPhysics);

    // Each frame, from GuestService: this game's hands, then every played body to its pose now.
    void OnUpdate(const UpdateEvent& acEvent) noexcept;

private:
    void OnFrames(const NotifyBodyFrames& acMessage) noexcept;
    void OnRests(const NotifyBodyRests& acMessage) noexcept;
    void ShowHost(uint64_t aTick) noexcept;
    // This game's copy of the host's actor (its form id), 0 for none.
    [[nodiscard]] uint32_t CopyOf(uint32_t aSessionId) const noexcept;

    World& m_world;
    GuestEngine& m_engine;
    GuestHands m_hands;
    BodyPlayback m_playback;
    std::set<BodyKey> m_following; // played from the host's frames (logged once)
    std::set<BodyKey> m_missing;   // not in this game (logged once)
    std::vector<entt::scoped_connection> m_connections;
};
