#include <Services/Host/HostSession.h>

#include <Services/Host/HostNetwork.h>
#include <Services/Host/HostAuthority.h>
#include <Services/Host/HostCharacters.h>
#include <Services/Host/HostGame.h>
#include <Services/Host/HostPlayers.h>
#include <Services/Host/HostSaveTransfer.h>
#include <Services/Host/JoinRefusal.h>

#include <BuildVersion.h>
#include <Messages/AuthenticationRequest.h>
#include <Messages/AuthenticationResponse.h>
#include <Messages/NotifyChatMessageBroadcast.h>
#include <Messages/NotifyPlayerJoined.h>
#include <Messages/NotifyPlayerLeft.h>
#include <Messages/NotifySettingsChange.h>
#include <Structs/LoadOrder.h>

HostSession::HostSession(HostAuthority& aAuthority, HostNetwork& aNetwork, HostPlayers& aPlayers, HostCharacters& aCharacters,
                         HostSaveTransfer& aTransfer, const HostGame& acGame) noexcept
    : m_network(aNetwork)
    , m_players(aPlayers)
    , m_characters(aCharacters)
    , m_transfer(aTransfer)
    , m_game(acGame)
{
    aAuthority.On(*this, &HostSession::OnAuthentication);
}

void HostSession::OnDifficultyChange(uint32_t aDifficulty) const noexcept
{
    // Not before the host's own player is in: "everyone but the host" would include it.
    const ConnectionId_t cHost = m_players.Host();
    if (!cHost)
        return;
    NotifySettingsChange notify;
    notify.Difficulty = aDifficulty;
    m_players.SendToOthers(notify, cHost);
}

void HostSession::OnAuthentication(const AuthenticationRequest& acRequest, ConnectionId_t aSender) noexcept
{
    using enum AuthenticationResponse::ResponseType;

    if (m_players.IsPlayer(aSender))
    {
        spdlog::warn("HostSession: {:x} asked to join again", aSender);
        return;
    }

    AuthenticationResponse response;
    response.Version = BuildCommit();
    response.Game = m_game.Versions;
    const auto refuse = [&](AuthenticationResponse::ResponseType aReason) {
        response.Type = aReason;
        m_network.Send(aSender, response);
        m_network.Kick(aSender);
        // A session event, for the host's chat.
        NotifyChatMessageBroadcast line;
        line.MessageType = kSystemMessage;
        line.ChatMessage = JoinRefusal::ForHost(aReason, acRequest, {BuildCommit(), m_game.Versions, m_game.Plugins}).c_str();
        if (const ConnectionId_t cHost = m_players.Host())
            m_network.Send(cHost, line);
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
    if (m_players.Count() >= cMaxPlayers)
    {
        spdlog::info("HostSession: {:x} '{}' found the session full: refused", aSender, acRequest.Username.c_str());
        return refuse(kSessionFull);
    }

    if (acRequest.WantsWorld)
    {
        const bool cKnown = m_game.HasRecord && m_game.HasRecord(acRequest.PlayerKey);
        if (!cKnown && !acRequest.HasCharacter)
        {
            spdlog::info("HostSession: {:x} '{}' has no character in the host's save and brings none: refused", aSender, acRequest.PlayerKey);
            return refuse(kNoCharacter);
        }
        spdlog::info("HostSession: {:x} '{}' joins ({}): the host's save follows", aSender, acRequest.PlayerKey,
                     cKnown ? "the host's save keeps its character" : "it brings its character");
        response.Type = kWorldFollows;
        m_network.Send(aSender, response);
        m_transfer.Want(aSender);
        return;
    }

    response.Type = kAccepted;
    response.PlayerId = m_players.Add(aSender, acRequest.Username, acRequest.Level, aSender == m_network.HostConnection(), acRequest.PlayerKey);
    response.CharacterId = m_characters.ReservePlayerCharacter(aSender);
    response.Difficulty = m_game.ReadDifficulty();
    m_network.Send(aSender, response);
    Announce(aSender);
}

void HostSession::Announce(ConnectionId_t aNewcomer) const noexcept
{
    const auto joined = [](const HostPlayers::Player& acPlayer) {
        NotifyPlayerJoined notify;
        notify.PlayerId = acPlayer.Id;
        notify.Username = acPlayer.Name;
        notify.Level = acPlayer.Level;
        notify.IsHost = acPlayer.IsHost;
        return notify;
    };
    const auto cNewcomer = joined(*m_players.Find(aNewcomer));
    m_players.ForEach([&](ConnectionId_t aConnection, const HostPlayers::Player& acPlayer) {
        if (aConnection == aNewcomer)
            return;
        m_network.Send(aNewcomer, joined(acPlayer));
        m_network.Send(aConnection, cNewcomer);
    });
}

void HostSession::OnPlayerLeft(ConnectionId_t aPlayer) const noexcept
{
    const auto* cpPlayer = m_players.Find(aPlayer);
    if (!cpPlayer)
        return;
    NotifyPlayerLeft notify;
    notify.PlayerId = cpPlayer->Id;
    notify.Username = cpPlayer->Name;
    m_players.SendToOthers(notify, aPlayer);
}
