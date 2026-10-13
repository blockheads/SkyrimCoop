#include <Services/Host/HostSession.h>

#include <GameServer.h>
#include <Services/Host/HostBridge.h>
#include <Services/Host/HostGame.h>
#include <Services/Host/JoinRefusal.h>

#include <BuildVersion.h>
#include <Messages/AuthenticationRequest.h>
#include <Messages/AuthenticationResponse.h>
#include <Messages/NotifyChatMessageBroadcast.h>
#include <Messages/NotifySettingsChange.h>
#include <Structs/LoadOrder.h>

HostSession::HostSession(HostBridge& aBridge, Server::GameServer& aServer, const HostGame& acGame) noexcept
    : m_server(aServer)
    , m_game(acGame)
{
    aBridge.On(*this, &HostSession::OnAuthentication);
}

ServerSettings HostSession::Settings() const noexcept
{
    // The co-op options are upstream's defaults until a settings screen sets them.
    ServerSettings settings{};
    settings.Difficulty = m_game.ReadDifficulty();
    settings.GreetingsEnabled = false;
    settings.PvpEnabled = false;
    settings.SyncPlayerHomes = false;
    settings.DeathSystemEnabled = true;
    settings.SyncPlayerCalendar = false;
    return settings;
}

void HostSession::Update() noexcept
{
    // Not before the host's own player is in: "everyone but the host" would include it.
    const ConnectionId_t cHost = m_server.HostConnection();
    const auto cSettings = Settings();
    if (!cHost || cSettings == m_sentSettings)
        return;

    const bool cFirst = !m_sentSettings;
    m_sentSettings = cSettings;
    if (cFirst)
        return; // everyone who joined got these
    NotifySettingsChange notify;
    notify.Settings = cSettings;
    m_server.SendToOthers(notify, cHost, TiltedPhoques::kReliable);
}

void HostSession::OnAuthentication(const AuthenticationRequest& acRequest, ConnectionId_t aSender) noexcept
{
    using enum AuthenticationResponse::ResponseType;

    if (m_server.IsAuthenticated(aSender))
    {
        spdlog::warn("HostSession: {:x} asked to join again", aSender);
        return;
    }

    AuthenticationResponse response;
    response.Version = BuildCommit();
    response.Game = m_game.Versions;
    const auto refuse = [&](AuthenticationResponse::ResponseType aReason) {
        response.Type = aReason;
        m_server.Send(aSender, response);
        m_server.Disconnect(aSender);
        // A session event, for the host's chat.
        NotifyChatMessageBroadcast line;
        line.MessageType = kSystemMessage;
        line.ChatMessage = JoinRefusal::ForHost(aReason, acRequest, {BuildCommit(), m_game.Versions, m_game.Plugins}).c_str();
        m_server.Send(m_server.HostConnection(), line);
    };

    if (acRequest.Version != BuildCommit())
    {
        spdlog::info("HostSession: {:x} '{}' runs build {}, the host {}: refused", aSender, acRequest.Username.c_str(),
                     acRequest.Version.c_str(), BuildCommit());
        return refuse(kWrongVersion);
    }
    if (acRequest.Game != m_game.Versions)
    {
        spdlog::info("HostSession: {:x} '{}' runs Skyrim {} with SKSE {}, the host Skyrim {} with SKSE {}: refused", aSender,
                     acRequest.Username.c_str(), GameVersions::Format(acRequest.Game.Runtime), GameVersions::Format(acRequest.Game.Skse),
                     GameVersions::Format(m_game.Versions.Runtime), GameVersions::Format(m_game.Versions.Skse));
        return refuse(kWrongGame);
    }
    if (const auto cDifferences = CompareLoadOrders(m_game.Plugins, acRequest.UserMods); !cDifferences.empty())
    {
        spdlog::info("HostSession: {:x} '{}' loads other plugins ({} differences): refused", aSender, acRequest.Username.c_str(), cDifferences.size());
        response.UserMods = m_game.Plugins;
        return refuse(kModsMismatch);
    }
    if (m_server.PlayerConnections().size() >= cMaxPlayers)
    {
        spdlog::info("HostSession: {:x} '{}' found the session full: refused", aSender, acRequest.Username.c_str());
        return refuse(kServerFull);
    }

    response.Type = kAccepted;
    response.PlayerId = m_server.AddPlayer(aSender, acRequest, response.UserMods);
    response.Settings = Settings();
    m_server.Send(aSender, response);
    m_server.AnnouncePlayer(aSender, acRequest.WorldSpaceId, acRequest.CellId);
}
