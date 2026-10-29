// A host load or party wipe: the chosen save to the guests, then
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
constexpr const char* kReloadSave = "SkyrimCoopWipe";
constexpr const char* kNoSave = "No previous gameplay save could be loaded. Choose a save or return to the menu.";
constexpr const char* kChosenMissing = "The selected save could not be read. Choose another save.";
constexpr const char* kLoadFailed = "The save could not be loaded. Choose a save or return to the menu.";
} // namespace

void HostService::LoadSave(const std::string& acName) noexcept
{
    if (!IsHosting() || m_loadingSave || m_checkpoint)
        return;
    m_checkpoint = m_world.ctx().at<IJoinBridge&>().ReadSave(acName);
    if (!m_checkpoint)
    {
        m_dispatcher.trigger(SessionNoticeEvent{kChosenMissing});
        return;
    }
    m_reloadRequested = true;
    m_readCheckpoint = false;
}

void HostService::UpdateReload() noexcept
{
    if (!m_reloadRequested || m_loadingSave)
        return;
    if (!m_readCheckpoint)
    {
        m_readCheckpoint = true;
        if (!m_checkpoint)
            m_checkpoint = m_world.ctx().at<IJoinBridge&>().PreviousSave();
        if (!m_checkpoint)
        {
            m_pAuthority->FailReload(kNoSave);
            m_dispatcher.trigger(SessionNoticeEvent{kNoSave});
            return;
        }
        m_pAuthority->SendCheckpoint(SaveTransfer::Split(SaveTransfer::Pack(*m_checkpoint)));
    }
    if (!m_checkpoint || !m_pAuthority->CheckpointDelivered())
        return;
    m_loadingSave = true;
    m_world.GetTransport().Close();
    if (!m_world.ctx().at<IJoinBridge&>().WriteAndLoad(kReloadSave, *m_checkpoint))
        OnGameLoadFailed(GameLoadFailedEvent{kReloadSave});
}

void HostService::OnGameLoaded(const GameLoadedEvent&) noexcept
{
    if (!IsHosting() || !m_reloadRequested)
        return;
    m_pConversation.reset();
    m_pAuthority.reset();
    m_pEngine.reset();
    m_reloadRequested = m_readCheckpoint = m_loadingSave = false;
    m_checkpoint.reset();
    MakeAuthority();
    m_world.GetTransport().ConnectLocal(*m_pNetwork);
    for (const auto& [cConnection, cRequest] : std::exchange(m_reloadJoins, {}))
        if (m_pNetwork->IsAlive(cConnection))
            m_pAuthority->Handle(cRequest, cConnection);
}

void HostService::InstallFrontDoor() noexcept
{
    m_pNetwork->SetRecipes(&m_world.GetRecipes());
    m_pNetwork->SetFrontDoor([this](const RequestMessage& acRequest, Net::ConnectionId_t aConnection) {
        if (m_reloadRequested && acRequest.GetOpcode() == kAuthenticationRequest)
        {
            if (m_readCheckpoint && !m_checkpoint)
            {
                AuthenticationResponse failed;
                failed.Type = AuthenticationResponse::ResponseType::kTransferFailed;
                m_pNetwork->Send(aConnection, failed);
                return true;
            }
            m_reloadJoins.insert_or_assign(aConnection, static_cast<const AuthenticationRequest&>(acRequest));
            return true;
        }
        return m_pAuthority->Handle(acRequest, aConnection);
    });
}

void HostService::OnGameLoadFailed(const GameLoadFailedEvent& acEvent) noexcept
{
    if (!IsHosting() || !m_loadingSave || acEvent.Name != kReloadSave)
        return;
    m_loadingSave = false;
    m_checkpoint.reset();
    m_pAuthority->FailReload(kLoadFailed);
    m_dispatcher.trigger(SessionNoticeEvent{kLoadFailed});
    AuthenticationResponse failed;
    failed.Type = AuthenticationResponse::ResponseType::kTransferFailed;
    for (const auto& [cConnection, cRequest] : std::exchange(m_reloadJoins, {}))
        m_pNetwork->Send(cConnection, failed);
}

void HostService::AnnounceEnd() const noexcept
{
    if (m_pAuthority)
        m_pNetwork->SetDisconnectReason(Net::Server::SessionEnded);
}
