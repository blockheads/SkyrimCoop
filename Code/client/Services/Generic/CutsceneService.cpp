#include <Services/CutsceneService.h>
#include <Services/TransportService.h>

#include <Events/ConnectedEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/CallEndedEvent.h>
#include <Events/RoutedCallEvent.h>
#include <Messages/NotifyPlayerLeft.h>
#include <Messages/Relayed.h>
#include <Papyrus/PartyNatives.h>
#include <ScriptValues.h>
#include <SessionIds.h>
#include <World.h>

CutsceneService::CutsceneService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept
    : m_world(aWorld)
    , m_transport(aTransport)
{
    Listen<ConnectedEvent, &CutsceneService::OnConnected>(m_connections, aDispatcher, this);
    Listen<DisconnectedEvent, &CutsceneService::OnDisconnected>(m_connections, aDispatcher, this);
    Listen<RoutedCallEvent, &CutsceneService::OnRoutedCall>(m_connections, aDispatcher, this);
    Listen<NotifyRoutedCall, &CutsceneService::OnNotifyRoutedCall>(m_connections, aDispatcher, this);
    Listen<CallEndedEvent, &CutsceneService::OnCallEnded>(m_connections, aDispatcher, this);
    Listen<NotifyCallEnded, &CutsceneService::OnNotifyCallEnded>(m_connections, aDispatcher, this);
    Listen<NotifyPlayerLeft, &CutsceneService::OnPlayerLeft>(m_connections, aDispatcher, this);
}

void CutsceneService::OnPlayerLeft(const NotifyPlayerLeft& acMessage) noexcept
{
    PartyNatives::GiveBackControls(acMessage.PlayerId); // what its cutscene still held here
    PartyNatives::ForgetCopies(acMessage.PlayerId);
}

void CutsceneService::OnConnected(const ConnectedEvent&) noexcept
{
    PartyNatives::SetInSession(true);
}

void CutsceneService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    PartyNatives::SetInSession(false);
    PartyNatives::GiveBackControls();
    PartyNatives::ForgetCopies();
}

void CutsceneService::OnRoutedCall(const RoutedCallEvent& acEvent) noexcept
{
    RoutedCallRequest request;
    request.To = acEvent.To;
    request.ReplyId = acEvent.ReplyId;
    request.Call = Utils::ToScriptCall(acEvent.Call);
    const auto cGuest = acEvent.GuestFormId ? Utils::GetSessionIdByFormId(acEvent.GuestFormId) : std::optional<uint32_t>(0);
    if (!m_transport.IsConnected() || !cGuest)
    {
        spdlog::warn("CutsceneService: {}.{} has no game to go to: not run", request.Call.Class, request.Call.Function);
        return;
    }
    request.SessionId = *cGuest;
    spdlog::info("CutsceneService: {}.{} goes to {}", request.Call.Class, request.Call.Function,
                 acEvent.To == CallTo::kHost ? "the host" : acEvent.To == CallTo::kOwner ? "its guest" : "the party");
    m_transport.Send(request);
}

void CutsceneService::OnNotifyRoutedCall(const NotifyRoutedCall& acMessage) noexcept
{
    if (acMessage.To != CallTo::kOthers)
        return;
    const auto& cWire = acMessage.Call;
    const auto cCall = Utils::ToLocalCall(cWire);
    if (!cCall)
    {
        spdlog::warn("CutsceneService: {}.{} from another game names a form not here", cWire.Class, cWire.Function);
        return;
    }
    std::string forms;
    if (const auto* cpSelf = std::get_if<LocalForm>(&cCall->Self))
        forms += std::format(" on {:X}", cpSelf->FormId);
    for (const auto& cArgument : cCall->Arguments)
        if (const auto* cpForm = std::get_if<LocalForm>(&cArgument))
            forms += std::format(" {:X}", cpForm->FormId);
    spdlog::info("CutsceneService: another game's script runs {}.{} here{}", cWire.Class, cWire.Function, forms);
    if (!PartyNatives::Run(*cCall, acMessage.Source, acMessage.ReplyId))
        spdlog::warn("CutsceneService: {}.{} could not be queued", cWire.Class, cWire.Function);
}

void CutsceneService::OnCallEnded(const CallEndedEvent& acEvent) noexcept
{
    if (!m_transport.IsConnected())
        return;
    CallEndedRequest request;
    request.Key = acEvent.Key;
    request.Result = Utils::ToScriptValue(acEvent.Result);
    m_transport.Send(request);
}

void CutsceneService::OnNotifyCallEnded(const NotifyCallEnded& acMessage) noexcept
{
    PartyNatives::EndCopy(acMessage.Source, acMessage.Key, Utils::ToLocalValue(acMessage.Result).value_or(std::monostate{}));
}
