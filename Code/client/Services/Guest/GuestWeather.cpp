#include <Services/Guest/GuestWeather.h>

#include <Bridges/GuestEngine.h>
#include <Events/UpdateEvent.h>
#include <Messages/NotifyWeatherChange.h>

GuestWeather::GuestWeather(entt::dispatcher& aDispatcher, GuestEngine& aEngine) noexcept
    : m_engine(aEngine)
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
}

void GuestWeather::OnUpdate(const UpdateEvent&) noexcept
{
    if (m_hostWeather)
        m_engine.HoldWeather(*m_hostWeather);
}
