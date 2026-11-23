#pragma once

#include <Events/Listen.h>
#include <Structs/GameId.h>

#include <optional>
#include <NoCopyMove.h>

struct UpdateEvent;
struct NotifyWeatherChange;
struct World;
class GuestEngine;

/**
 * @brief A guest shows the weather the host gives its region (HostWeather) in place of its own, faded into; its own
 * again once this is gone. It tells the host its player's weather region when that changes.
 */
struct GuestWeather
{
    GuestWeather(World& aWorld, entt::dispatcher& aDispatcher, GuestEngine& aEngine) noexcept;
    ~GuestWeather() noexcept;

    NO_COPY_MOVE(GuestWeather);

    // Each frame, from GuestService (the parts subscribe to no update: they come and go inside one).
    void OnUpdate(const UpdateEvent& acEvent) noexcept;

private:
    void OnWeatherChange(const NotifyWeatherChange& acMessage) noexcept;

    World& m_world;
    GuestEngine& m_engine;
    std::optional<GameId> m_hostWeather;
    // The region the host was told (none sent yet: the first update sends it).
    std::optional<GameId> m_sentRegion;

    std::vector<entt::scoped_connection> m_connections;
};
