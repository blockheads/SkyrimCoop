#include <Services/CutsceneService.h>
#include <Services/TransportService.h>

#include <Events/ConnectedEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/PartyNativeEvent.h>
#include <Messages/NotifyPartyNative.h>
#include <Messages/NotifyPlayerLeft.h>
#include <Messages/Relayed.h>
#include <Papyrus/PartyNatives.h>
#include <ScriptValues.h>
#include <World.h>

CutsceneService::CutsceneService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept
    : m_world(aWorld)
    , m_transport(aTransport)
{
    m_connectedConnection = aDispatcher.sink<ConnectedEvent>().connect<&CutsceneService::OnConnected>(this);
    m_disconnectedConnection = aDispatcher.sink<DisconnectedEvent>().connect<&CutsceneService::OnDisconnected>(this);
    m_partyNativeConnection = aDispatcher.sink<PartyNativeEvent>().connect<&CutsceneService::OnPartyNative>(this);
    m_notifyConnection = aDispatcher.sink<NotifyPartyNative>().connect<&CutsceneService::OnNotifyPartyNative>(this);
    m_playerLeftConnection = aDispatcher.sink<NotifyPlayerLeft>().connect<&CutsceneService::OnPlayerLeft>(this);
}

void CutsceneService::OnPlayerLeft(const NotifyPlayerLeft& acMessage) noexcept
{
    PartyNatives::GiveBackControls(acMessage.PlayerId); // what its cutscene still held here
}

void CutsceneService::OnConnected(const ConnectedEvent&) noexcept
{
    PartyNatives::SetInSession(true);
}

void CutsceneService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    PartyNatives::SetInSession(false);
    PartyNatives::GiveBackControls();
}

void CutsceneService::OnPartyNative(const PartyNativeEvent& acEvent) noexcept
{
    if (!m_transport.IsConnected())
        return;
    PartyNativeRequest request;
    request.Call = Utils::ToScriptCall(acEvent.Call);
    spdlog::info("CutsceneService: {}.{} goes to the party", request.Call.Class, request.Call.Function);
    m_transport.Send(request);
}

void CutsceneService::OnNotifyPartyNative(const NotifyPartyNative& acMessage) noexcept
{
    const auto& cWire = acMessage.Call;
    const auto cCall = Utils::ToLocalCall(cWire);
    if (!cCall)
    {
        spdlog::warn("CutsceneService: {}.{} from another game names a form not here", cWire.Class, cWire.Function);
        return;
    }
    spdlog::info("CutsceneService: another game's script runs {}.{} here", cWire.Class, cWire.Function);
    if (!PartyNatives::Run(*cCall, acMessage.Source))
        spdlog::warn("CutsceneService: {}.{} could not be queued", cWire.Class, cWire.Function);
}
