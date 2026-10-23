#include <Services/GuestService.h>

#include <Bridges/GuestEngine.h>
#include <Papyrus/GlobalCall.h>
#include <Events/ConnectedEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/UpdateEvent.h>
#include <Messages/Relayed.h>
#include <Messages/PlayerNativeResultRequest.h>
#include <Events/WorldCallEvent.h>
#include <Papyrus/PlayerNatives.h>
#include <Services/Guest/GuestClock.h>
#include <Services/Guest/GuestWeather.h>
#include <Services/Guest/GuestWorldState.h>
#include <Services/Guest/GuestDialogue.h>
#include <Services/HostService.h>
#include <Services/RunnerService.h>
#include <Services/TransportService.h>
#include <Utils.h>
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
    m_playerNativeConnection = aDispatcher.sink<NotifyPlayerNative>().connect<&GuestService::OnPlayerNative>(this);
    m_worldCallConnection = aDispatcher.sink<WorldCallEvent>().connect<&GuestService::OnWorldCall>(this);
}

GuestService::~GuestService() noexcept = default;

void GuestService::OnConnected(const ConnectedEvent&) noexcept
{
    // The host joins its own session too: its world is its own.
    if (m_world.GetHostService().IsHosting())
        return;
    m_pGuest = std::make_unique<Guest>(m_world, m_dispatcher);
    PlayerNatives::SetGuest(true);
}

void GuestService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    PlayerNatives::SetGuest(false);
    m_pGuest.reset();
}

void GuestService::OnPlayerNative(const NotifyPlayerNative& acMessage) noexcept
{
    if (!m_pGuest)
        return;
    spdlog::info("GuestService: the host's script runs {}.{} for this game's player", acMessage.Call.Class, acMessage.Call.Function);
    // One returning a value: the host's script waits for it.
    const uint32_t cReplyId = acMessage.ReplyId;
    GlobalCall::OnReturn onReturn;
    if (cReplyId)
        onReturn = [this, cReplyId](LocalValue aResult) {
            m_world.GetRunner().Queue([this, cReplyId, cResult = std::move(aResult)] { SendNativeResult(cReplyId, cResult); });
        };
    if (m_pGuest->Engine.RunPlayerNative(acMessage, std::move(onReturn)))
        return;
    spdlog::warn("GuestService: {}.{} did not run", acMessage.Call.Class, acMessage.Call.Function);
    if (cReplyId)
        SendNativeResult(cReplyId, {});
}

void GuestService::OnWorldCall(const WorldCallEvent& acEvent) noexcept
{
    if (!m_pGuest)
        return;
    WorldCallRequest request;
    request.Call = Utils::ToScriptCall(acEvent.Call);
    m_world.GetTransport().Send(request);
}

void GuestService::SendNativeResult(uint32_t aReplyId, const LocalValue& acResult) noexcept
{
    PlayerNativeResultRequest request;
    request.ReplyId = aReplyId;
    request.Result = Utils::ToScriptValue(acResult);
    m_world.GetTransport().Send(request);
}

void GuestService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    if (!m_pGuest)
        return;
    m_pGuest->Clock.OnUpdate(acEvent);
    m_pGuest->Weather.OnUpdate(acEvent);
    m_pGuest->WorldState.OnUpdate(acEvent);
}
