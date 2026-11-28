// The host's saves and the guests (docs/design/join-flow.md, "Save sync"): each save sent ahead as it is made; a host
// load's save (a party wipe waits for one) to the guests, then loaded here, and the host's rules made again.
#include <game/HostService.h>
#include <join/IJoinBridge.h>
#include <combat/HostAggro.h>

#include <ports/IHostEngine.h>
#include <join/GameLoadFailedEvent.h>
#include <join/GameLoadedEvent.h>
#include <join/SaveWrittenEvent.h>
#include <game/HostGame.h>
#include <join/JoinService.h>
#include <core/Profile.h>
#include <core/SessionNoticeEvent.h>
#include <Messages/AuthenticationResponse.h>
#include <game/HostAuthority.h>
#include <dialogue/HostConversation.h>
#include <core/HostNetwork.h>
#include <join/SaveLibrary.h>
#include <join/SaveNames.h>
#include <core/TransportService.h>
#include <core/World.h>

namespace
{
constexpr const char* kReloadSave = SaveNames::kReload;
constexpr const char* kChosenMissing = "The selected save could not be read. Choose another save.";
constexpr const char* kLoadFailed = "The save could not be loaded. Choose a save or return to the menu.";
} // namespace

void HostService::BindSaves(HostGame& aGame) noexcept
{
    // Kept across a load, which makes the rules again.
    if (!m_pSaves)
        m_pSaves = std::make_unique<SaveLibrary>(m_world.ctx().at<IJoinBridge&>());
    aGame.SaveWorld = [this]() -> std::optional<std::string> {
        PROFILE_ZONE("Host save preparation");
        auto files = m_world.ctx().at<IJoinBridge&>().SaveNow(JoinService::kTransferSave);
        if (!files)
            return std::nullopt;
        return m_pSaves->Add(SaveTransfer::MakeImage(*files));
    };
    aGame.EncodeSave = [this](const std::string& acSave, const std::vector<std::string>& acHeld) { return m_pSaves->Encode(acSave, acHeld); };
}

void HostService::OnSaveWritten(const SaveWrittenEvent& acEvent) noexcept
{
    // Our own (a join's, a reload's scratch save) go their own way.
    if (!IsHosting() || m_reloadRequested || !m_pAuthority || !m_pAuthority->HasGuests() || acEvent.Name.starts_with("SkyrimCoop"))
        return;
    auto& bridge = m_world.ctx().at<IJoinBridge&>();
    auto files = bridge.ReadSave(acEvent.Name);
    if (!files)
        return;
    spdlog::info("HostService: {} is written: its image goes ahead to the guests", acEvent.Name);
    bridge.OffThread([this, files = std::move(*files)]() -> std::function<void()> {
        auto pImage = SaveTransfer::MakeImage(files);
        return [this, pImage = std::move(pImage)] {
            if (pImage && m_pSaves && m_pAuthority && !m_reloadRequested)
                m_pAuthority->OfferSave(m_pSaves->Add(pImage));
        };
    });
}

void HostService::LoadSave(const std::string& acName) noexcept
{
    if (!IsHosting() || m_loadingSave || m_checkpoint)
        return;
    m_checkpoint = m_world.ctx().at<IJoinBridge&>().ReadSave(acName);
    m_checkpointCharacter = SaveNames::CharacterOf(acName);
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
            return;
        const std::string cSave = m_pSaves->Add(SaveTransfer::MakeImage(*m_checkpoint));
        if (cSave.empty())
        {
            m_checkpoint.reset();
            m_pAuthority->FailReload(kChosenMissing);
            m_dispatcher.trigger(SessionNoticeEvent{kChosenMissing});
            return;
        }
        m_pAuthority->SendCheckpoint(cSave);
    }
    if (!m_checkpoint || !m_pAuthority->CheckpointDelivered())
        return;
    m_loadingSave = true;
    m_world.GetTransport().Close();
    if (!m_world.ctx().at<IJoinBridge&>().WriteAndLoad(kReloadSave, *m_checkpoint))
    {
        m_world.ctx().at<IJoinBridge&>().DeleteSave(kReloadSave); // a part may be written
        OnGameLoadFailed(GameLoadFailedEvent{kReloadSave});
    }
}

void HostService::OnGameLoaded(const GameLoadedEvent&) noexcept
{
    if (!IsHosting() || !m_reloadRequested)
        return;
    // The scratch save's name carries no character: the checkpoint's own again, for its saves.
    m_world.ctx().at<IJoinBridge&>().SetCharacter(m_checkpointCharacter);
    m_pConversation.reset();
    m_pAuthority.reset();
    m_pAggro.reset();
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
