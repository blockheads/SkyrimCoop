#include <Services/GuestService.h>

#include <Bridges/GuestEngine.h>
#include <Events/ConnectedEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/UpdateEvent.h>
#include <Services/Guest/GuestClock.h>
#include <Services/Guest/GuestWeather.h>
#include <Services/Guest/GuestWorldState.h>
#include <Services/Guest/GuestDialogue.h>
#include <Services/HostService.h>
#include <World.h>

// The parts, after the engine they apply through.
struct GuestService::Guest
{
    Guest(World& aWorld, entt::dispatcher& aDispatcher) noexcept
        : Engine(aWorld.GetModSystem())
        , Clock(aWorld, aDispatcher, Engine)
        , Weather(aDispatcher, Engine)
        , WorldState(aWorld, aDispatcher, Engine)
        , Dialogue(aWorld, aDispatcher, aWorld.ctx().at<DialogueService>())
    {
    }

    GuestEngine Engine;
    GuestClock Clock;
    GuestWeather Weather;
    GuestWorldState WorldState;
    GuestDialogue Dialogue;
};

GuestService::GuestService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
    , m_dispatcher(aDispatcher)
{
    m_connectedConnection = aDispatcher.sink<ConnectedEvent>().connect<&GuestService::OnConnected>(this);
    m_disconnectedConnection = aDispatcher.sink<DisconnectedEvent>().connect<&GuestService::OnDisconnected>(this);
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&GuestService::OnUpdate>(this);
}

GuestService::~GuestService() noexcept = default;

void GuestService::OnConnected(const ConnectedEvent&) noexcept
{
    // The host joins its own session too: its world is its own.
    if (!m_world.GetHostService().IsHosting())
        m_pGuest = std::make_unique<Guest>(m_world, m_dispatcher);
}

void GuestService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    m_pGuest.reset();
}

void GuestService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    if (!m_pGuest)
        return;
    m_pGuest->Clock.OnUpdate(acEvent);
    m_pGuest->Weather.OnUpdate(acEvent);
    m_pGuest->WorldState.OnUpdate(acEvent);
}
