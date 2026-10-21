#pragma once

#include <Structs/GameId.h>

#include <optional>

struct UpdateEvent;
struct NotifyWeatherChange;
class GuestEngine;

/**
 * @brief A guest shows the host's weather (HostWeather) in place of its own; its own again once this is gone.
 */
struct GuestWeather
{
    GuestWeather(entt::dispatcher& aDispatcher, GuestEngine& aEngine) noexcept;
    ~GuestWeather() noexcept;

    TP_NOCOPYMOVE(GuestWeather);

private:
    void OnWeatherChange(const NotifyWeatherChange& acMessage) noexcept;
    void OnUpdate(const UpdateEvent& acEvent) noexcept;

    GuestEngine& m_engine;
    std::optional<GameId> m_hostWeather;

    entt::scoped_connection m_weatherConnection;
    entt::scoped_connection m_updateConnection;
};
