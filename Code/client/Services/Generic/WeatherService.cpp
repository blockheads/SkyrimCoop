#include <Services/WeatherService.h>

#include <Events/UpdateEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/PartyJoinedEvent.h>
#include <Events/PartyLeftEvent.h>

#include <Messages/RequestWeatherChange.h>
#include <Messages/NotifyWeatherChange.h>
#include <Messages/RequestCurrentWeather.h>

#include <Bridges/IWorldBridge.h>
#include <Bridges/IFormBridge.h>

WeatherService::WeatherService(World& aWorld, TransportService& aTransport, entt::dispatcher& aDispatcher)
    : m_world(aWorld)
    , m_transport(aTransport)
    , m_worldBridge(aWorld.ctx().at<IWorldBridge&>())
    , m_formBridge(aWorld.ctx().at<IFormBridge&>())
{
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&WeatherService::OnUpdate>(this);
    m_disconnectConnection = aDispatcher.sink<DisconnectedEvent>().connect<&WeatherService::OnDisconnected>(this);
    m_partyJoinedConnection = aDispatcher.sink<PartyJoinedEvent>().connect<&WeatherService::OnPartyJoinedEvent>(this);
    m_partyLeftConnection = aDispatcher.sink<PartyLeftEvent>().connect<&WeatherService::OnPartyLeftEvent>(this);
    m_playerAddedConnection = m_world.on_destroy<WaitingFor3D>().connect<&WeatherService::OnWaitingFor3DRemoved>(this);
    m_playerRemovedConnection = m_world.on_destroy<PlayerComponent>().connect<&WeatherService::OnPlayerComponentRemoved>(this);
    m_weatherChangeConnection = aDispatcher.sink<NotifyWeatherChange>().connect<&WeatherService::OnWeatherChange>(this);
}

void WeatherService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    RunWeatherUpdates(acEvent.Delta);
}

void WeatherService::OnDisconnected(const DisconnectedEvent& acEvent) noexcept
{
    ToggleGameWeatherSystem(true);
}

void WeatherService::OnPartyJoinedEvent(const PartyJoinedEvent& acEvent) noexcept
{
    if (!acEvent.IsLeader)
    {
        // TODO: why is this loop here? Party should always have a leader.
        auto view = m_world.view<PlayerComponent>();
        const auto& partyService = m_world.GetPartyService();

        for (auto entity : view)
        {
            const auto& playerComponent = view.get<PlayerComponent>(entity);
            if (playerComponent.Id == partyService.GetLeaderPlayerId())
            {
                ToggleGameWeatherSystem(false);
                break;
            }
        }
    }
    else
    {
        const uint32_t currentWeatherId = m_worldBridge.GetCurrentWeatherId();
        if (currentWeatherId == 0)
        {
            m_cachedWeatherId = 0;
            return;
        }

        // Potentially sets cached weather to map weather.
        // When the player closes the map, it'll send out the proper weather on the next update.
        m_cachedWeatherId = currentWeatherId;

        // This is the map weather, should not be synced.
        if (currentWeatherId == 0xA6858)
            return;

        RequestWeatherChange request{};

        auto& modSystem = m_world.GetModSystem();
        if (!modSystem.GetServerModId(currentWeatherId, request.Id))
        {
            spdlog::error("{}: : weather server ID not found, form id: {:X}", __FUNCTION__, currentWeatherId);
            return;
        }

        m_transport.Send(request);
    }
}

void WeatherService::OnPartyLeftEvent(const PartyLeftEvent& acEvent) noexcept
{
    ToggleGameWeatherSystem(true);
}

// TODO: OnPlayerComponentAdded() instead? Does PlayerComponent exist already by then?
void WeatherService::OnWaitingFor3DRemoved(entt::registry& aRegistry, entt::entity aEntity) noexcept
{
    const auto* pPlayerComponent = m_world.try_get<PlayerComponent>(aEntity);
    if (!pPlayerComponent)
        return;

    const auto& partyService = m_world.GetPartyService();
    if (!partyService.IsInParty() || partyService.IsLeader())
        return;

    if (partyService.GetLeaderPlayerId() == pPlayerComponent->Id)
        ToggleGameWeatherSystem(false);
}

void WeatherService::OnPlayerComponentRemoved(entt::registry& aRegistry, entt::entity aEntity) noexcept
{
    const auto& playerComponent = m_world.get<PlayerComponent>(aEntity);

    const auto& partyService = m_world.GetPartyService();
    if (!partyService.IsInParty() || partyService.IsLeader())
        return;

    if (partyService.GetLeaderPlayerId() == playerComponent.Id)
        ToggleGameWeatherSystem(true);
}

void WeatherService::OnWeatherChange(const NotifyWeatherChange& acMessage) noexcept
{
    auto& modSystem = m_world.GetModSystem();
    const uint32_t weatherId = modSystem.GetGameId(acMessage.Id);

    if (!m_formBridge.FormExists(weatherId))
    {
        spdlog::error("{}: : weather not found, form id: {:X}", __FUNCTION__, acMessage.Id.ModId + acMessage.Id.BaseId);
        return;
    }

    m_worldBridge.ForceWeather(weatherId, false);

    m_cachedWeatherId = weatherId;
}

void WeatherService::RunWeatherUpdates(const double acDelta) noexcept
{
    const uint32_t currentWeatherId = m_worldBridge.GetCurrentWeatherId();
    if (currentWeatherId == 0)
    {
        if (m_world.GetPartyService().IsLeader())
            m_cachedWeatherId = 0;
        else
            SetCachedWeather();

        return;
    }

    // This is the map weather, should not be synced.
    if (currentWeatherId == 0xA6858)
        return;

    // Have to manually check each frame because there's no singular SetWeather being used in-game.
    if (currentWeatherId == m_cachedWeatherId)
        return;

    if (m_world.GetPartyService().IsLeader())
    {
        m_cachedWeatherId = currentWeatherId;

        RequestWeatherChange request{};

        auto& modSystem = m_world.GetModSystem();
        if (!modSystem.GetServerModId(currentWeatherId, request.Id))
        {
            spdlog::error("{}: : weather server ID not found, form id: {:X}", __FUNCTION__, currentWeatherId);
            return;
        }

        m_transport.Send(request);
    }
    else
    {
        SetCachedWeather();
    }
}

void WeatherService::ToggleGameWeatherSystem(bool aToggle) noexcept
{
    if (aToggle)
        m_worldBridge.ReleaseWeatherOverride();
    else
        m_transport.Send(RequestCurrentWeather());

    m_cachedWeatherId = 0;
}

void WeatherService::SetCachedWeather() noexcept
{
    if (m_cachedWeatherId == 0)
        return;

    if (!m_formBridge.FormExists(m_cachedWeatherId))
    {
        spdlog::error("{}: : weather not found, form id: {:X}", __FUNCTION__, m_cachedWeatherId);
        return;
    }

    m_worldBridge.ForceWeather(m_cachedWeatherId, false);
}
