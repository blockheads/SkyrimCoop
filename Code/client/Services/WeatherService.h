#pragma once

#include <Structs/GameId.h>

#include <optional>

struct World;
struct UpdateEvent;
struct DisconnectedEvent;
struct NotifyWeatherChange;
class IWorldBridge;
class IFormBridge;

/**
 * @brief The session's weather is the host's (HostWeather): the host shares what its game shows,
 * and a peer holds the host's weather in place of its own while in the session.
 */
struct WeatherService
{
    WeatherService(World& aWorld, entt::dispatcher& aDispatcher);
    ~WeatherService() noexcept = default;

    TP_NOCOPYMOVE(WeatherService);

    // This game's weather to share, as a server id (none: no sky, or the map's).
    [[nodiscard]] std::optional<GameId> ReadSharedWeather() const noexcept;

protected:
    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    void OnDisconnected(const DisconnectedEvent& acEvent) noexcept;
    void OnWeatherChange(const NotifyWeatherChange& acMessage) noexcept;

private:
    World& m_world;
    IWorldBridge& m_worldBridge;
    IFormBridge& m_formBridge;

    // The host's weather, held in place of this game's own (0: none, as on the host).
    uint32_t m_hostWeatherId{};

    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_disconnectConnection;
    entt::scoped_connection m_weatherChangeConnection;
};
