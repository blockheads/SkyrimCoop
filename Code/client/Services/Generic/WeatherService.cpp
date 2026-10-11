#include <Services/WeatherService.h>

#include <Events/UpdateEvent.h>
#include <Events/DisconnectedEvent.h>

#include <Messages/NotifyWeatherChange.h>

#include <Bridges/IWorldBridge.h>
#include <Bridges/IFormBridge.h>

namespace
{
// The world map shows its own weather while open: never shared, never overridden.
constexpr uint32_t kMapWeatherFormId = 0xA6858;
} // namespace

WeatherService::WeatherService(World& aWorld, entt::dispatcher& aDispatcher)
    : m_world(aWorld)
    , m_worldBridge(aWorld.ctx().at<IWorldBridge&>())
    , m_formBridge(aWorld.ctx().at<IFormBridge&>())
{
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&WeatherService::OnUpdate>(this);
    m_disconnectConnection = aDispatcher.sink<DisconnectedEvent>().connect<&WeatherService::OnDisconnected>(this);
    m_weatherChangeConnection = aDispatcher.sink<NotifyWeatherChange>().connect<&WeatherService::OnWeatherChange>(this);
}

std::optional<GameId> WeatherService::ReadSharedWeather() const noexcept
{
    const uint32_t cWeatherId = m_worldBridge.GetCurrentWeatherId();
    if (cWeatherId == 0 || cWeatherId == kMapWeatherFormId)
        return std::nullopt;

    GameId id;
    if (!m_world.GetModSystem().GetServerModId(cWeatherId, id))
    {
        spdlog::error("{}: weather server ID not found, form id: {:X}", __FUNCTION__, cWeatherId);
        return std::nullopt;
    }
    return id;
}

void WeatherService::OnUpdate(const UpdateEvent&) noexcept
{
    if (m_hostWeatherId == 0)
        return;

    // The game changes its weather in many places (no single SetWeather): put the host's back
    // whenever it did.
    const uint32_t cWeatherId = m_worldBridge.GetCurrentWeatherId();
    if (cWeatherId != m_hostWeatherId && cWeatherId != kMapWeatherFormId)
        m_worldBridge.ForceWeather(m_hostWeatherId, false);
}

void WeatherService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    if (m_hostWeatherId == 0)
        return;

    m_worldBridge.ReleaseWeatherOverride();
    m_hostWeatherId = 0;
}

void WeatherService::OnWeatherChange(const NotifyWeatherChange& acMessage) noexcept
{
    const uint32_t cWeatherId = m_world.GetModSystem().GetGameId(acMessage.Id);
    if (!m_formBridge.FormExists(cWeatherId))
    {
        spdlog::error("{}: weather not found, form id: {:X}", __FUNCTION__, acMessage.Id.ModId + acMessage.Id.BaseId);
        return;
    }

    m_hostWeatherId = cWeatherId;
    m_worldBridge.ForceWeather(m_hostWeatherId, false);
}
