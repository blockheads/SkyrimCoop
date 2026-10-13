#include <Services/Host/HostSession.h>

#include <GameServer.h>
#include <Services/Host/HostBridge.h>
#include <Services/Host/JoinRefusal.h>

#include <BuildVersion.h>
#include <Messages/AuthenticationRequest.h>
#include <Messages/AuthenticationResponse.h>
#include <Messages/NotifyChatMessageBroadcast.h>

HostSession::HostSession(HostBridge& aBridge, Server::GameServer& aServer) noexcept
    : m_server(aServer)
{
    aBridge.On(*this, &HostSession::OnAuthentication);
}

ServerSettings HostSession::Settings() noexcept
{
    // Upstream's server defaults (docs/design/join-flow.md, stage 4: the host's own values next).
    ServerSettings settings{};
    settings.Difficulty = 4;
    settings.GreetingsEnabled = false;
    settings.PvpEnabled = false;
    settings.SyncPlayerHomes = false;
    settings.DeathSystemEnabled = true;
    settings.SyncPlayerCalendar = false;
    return settings;
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
    const auto refuse = [&](AuthenticationResponse::ResponseType aReason) {
        response.Type = aReason;
        m_server.Send(aSender, response);
        m_server.Disconnect(aSender);
        // A session event, for the host's chat.
        NotifyChatMessageBroadcast line;
        line.MessageType = kSystemMessage;
        line.ChatMessage = JoinRefusal::ForHost(aReason, acRequest.Username.c_str(), acRequest.Version.c_str(), BuildCommit()).c_str();
        m_server.Send(m_server.HostConnection(), line);
    };

    if (acRequest.Version != BuildCommit())
    {
        spdlog::info("HostSession: {:x} '{}' runs build {}, the host {}: refused", aSender, acRequest.Username.c_str(),
                     acRequest.Version.c_str(), BuildCommit());
        return refuse(kWrongVersion);
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
