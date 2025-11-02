#pragma once

#include <Events/PacketEvent.h>

struct RequestActorValueChanges;
struct RequestActorMaxValueChanges;
struct RequestHealthChangeBroadcast;
struct RequestDeathStateChange;

namespace Server
{

struct World;
struct UpdateEvent;

/**
 * @brief Broadcasts changes in (max) actor values and updates them server side.
 */
struct ActorValueService
{
    ActorValueService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~ActorValueService() noexcept = default;

    TP_NOCOPYMOVE(ActorValueService);

private:
    World& m_world;

    void OnActorValueChanges(const PacketEvent<RequestActorValueChanges>& acMessage) const noexcept;
    void OnActorMaxValueChanges(const PacketEvent<RequestActorMaxValueChanges>& acMessage) const noexcept;
    void OnHealthChangeBroadcast(const PacketEvent<RequestHealthChangeBroadcast>& acMessage) const noexcept;
    void OnDeathStateChange(const PacketEvent<RequestDeathStateChange>& acMessage) const noexcept;

    entt::scoped_connection m_updateHealthConnection;
    entt::scoped_connection m_updateMaxValueConnection;
    entt::scoped_connection m_updateDeltaHealthConnection;
    entt::scoped_connection m_deathStateConnection;
};

} // namespace Server
