#include <Services/Guest/GuestWeather.h>

#include <Bridges/IGuestEngine.h>
#include <Events/UpdateEvent.h>
#include <Messages/NotifyWeatherChange.h>
#include <Messages/WeatherRegionRequest.h>
#include <Services/TransportService.h>
#include <World.h>
#include <Diagnostics/Profile.h>

GuestWeather::GuestWeather(World& aWorld, entt::dispatcher& aDispatcher, IGuestEngine& aEngine) noexcept
    : m_world(aWorld)
    , m_engine(aEngine)
{
    Listen<NotifyWeatherChange, &GuestWeather::OnWeatherChange>(m_connections, aDispatcher, this);
}

GuestWeather::~GuestWeather() noexcept
{
    if (m_hostWeather)
        m_engine.ReleaseWeather();
}

void GuestWeather::OnWeatherChange(const NotifyWeatherChange& acMessage) noexcept
{
    if (!m_engine.HoldWeather(acMessage.Id))
    {
        spdlog::error("{}: the host's weather {:X}:{:X} is not in this game", __FUNCTION__, acMessage.Id.ModId, acMessage.Id.BaseId);
        return;
    }
    m_hostWeather = acMessage.Id;
    spdlog::info("{}: the host gives {:X}:{:X} for region {:X}", __FUNCTION__, acMessage.Id.ModId, acMessage.Id.BaseId,
                 m_sentRegion ? m_sentRegion->BaseId : 0);
}

void GuestWeather::OnUpdate(const UpdateEvent&) noexcept
{
    PROFILE_ZONE("GuestWeather::OnUpdate");
    // The region changes on the player's cell events (PlayerRegionState 41264): a read, sent when it changed.
    const GameId cRegion = m_engine.ReadWeatherRegion();
    if (cRegion != m_sentRegion)
    {
        WeatherRegionRequest request;
        request.Region = cRegion;
        if (m_world.GetTransport().Send(request))
            m_sentRegion = cRegion;
    }
    if (m_hostWeather)
        m_engine.HoldWeather(*m_hostWeather);
}
