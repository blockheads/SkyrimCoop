// HostService's half for a party wipe (docs/design/death.md, "Wipe reload"): the previous save to the guests, then
// loaded here, and the host's rules made again on the same listener.
#include <Services/HostService.h>

#include <Bridges/HostEngine.h>
#include <Events/GameLoadFailedEvent.h>
#include <Events/GameLoadedEvent.h>
#include <Events/SessionNoticeEvent.h>
#include <Messages/AuthenticationResponse.h>
#include <Services/Host/HostAuthority.h>
#include <Services/Host/HostConversation.h>
#include <Services/Host/HostNetwork.h>
#include <Services/TransportService.h>
#include <World.h>

namespace
{
constexpr const char* kWipeSave = "SkyrimCoopWipe";
constexpr const char* kNoSave = "No previous gameplay save could be loaded. Choose a save or return to the menu.";
constexpr const char* kLoadFailed = "The previous save could not be loaded. Choose a save or return to the menu.";
} // namespace

void HostService::UpdateReload() noexcept
{
    if (!m_reloadRequested || m_loadingPrevious)
        return;
    if (!m_readCheckpoint)
    {
        m_readCheckpoint = true;
        m_checkpoint = m_world.ctx().at<IJoinBridge&>().PreviousSave();
        if (!m_checkpoint)
        {
            m_pAuthority->FailReload(kNoSave);
            return;
        }
        m_pAuthority->SendCheckpoint(SaveTransfer::Split(SaveTransfer::Pack(*m_checkpoint)));
    }
    if (!m_checkpoint || !m_pAuthority->CheckpointDelivered())
        return;
    m_loadingPrevious = true;
    m_world.GetTransport().Close();
    if (!m_world.ctx().at<IJoinBridge&>().WriteAndLoad(kWipeSave, *m_checkpoint))
    {
        m_loadingPrevious = false;
        m_checkpoint.reset();
        m_pAuthority->FailReload(kLoadFailed);
    }
}

void HostService::OnGameLoaded(const GameLoadedEvent&) noexcept
{
    if (!IsHosting() || !m_reloadRequested)
        return;
    m_pConversation.reset();
    m_pAuthority.reset();
    m_pEngine.reset();
    m_reloadRequested = m_readCheckpoint = m_loadingPrevious = false;
    m_checkpoint.reset();
    MakeAuthority();
    m_world.GetTransport().ConnectLocal(*m_pNetwork);
    for (const auto& [cConnection, cRequest] : std::exchange(m_reloadJoins, {}))
        if (m_pNetwork->IsAlive(cConnection))
            m_pAuthority->Handle(cRequest, cConnection);
}

void HostService::InstallFrontDoor() noexcept
{
    m_pNetwork->SetFrontDoor([this](const RequestMessage& acRequest, Net::ConnectionId_t aConnection) {
        if (m_reloadRequested && acRequest.GetOpcode() == kAuthenticationRequest)
        {
            m_reloadJoins.insert_or_assign(aConnection, static_cast<const AuthenticationRequest&>(acRequest));
            return true;
        }
        return m_pAuthority->Handle(acRequest, aConnection);
    });
}

void HostService::OnGameLoadFailed(const GameLoadFailedEvent& acEvent) noexcept
{
    if (!IsHosting() || !m_loadingPrevious || acEvent.Name != kWipeSave)
        return;
    m_loadingPrevious = false;
    m_checkpoint.reset();
    m_pAuthority->FailReload(kLoadFailed);
    m_dispatcher.trigger(SessionNoticeEvent{kLoadFailed});
    AuthenticationResponse failed;
    failed.Type = AuthenticationResponse::ResponseType::kTransferFailed;
    for (const auto& [cConnection, cRequest] : std::exchange(m_reloadJoins, {}))
        m_pNetwork->Send(cConnection, failed);
}
