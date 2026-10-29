#include <Services/SteamService.h>

#include <Events/ConnectedEvent.h>
#include <Events/GameLoadedEvent.h>
#include <Events/MenuEvent.h>
#include <Events/NewGameEvent.h>
#include <Events/SessionNoticeEvent.h>
#include <Events/UpdateEvent.h>
#include <Services/Host/HostNetwork.h>
#include <Services/HostService.h>
#include <Services/JoinService.h>
#include <Bridges/IJoinBridge.h>
#include <Services/TransportService.h>
#include <World.h>

#include <LoopbackPort.h>

#include <algorithm>

SteamService::SteamService(World& aWorld, entt::dispatcher& aDispatcher, ISteamBridge& aSteam) noexcept
    : m_world(aWorld)
    , m_dispatcher(aDispatcher)
    , m_steam(aSteam)
{
    Listen<UpdateEvent, &SteamService::OnUpdate>(m_connections, aDispatcher, this);
    Listen<GameLoadedEvent, &SteamService::OnGameLoaded>(m_connections, aDispatcher, this);
    Listen<NewGameEvent, &SteamService::OnNewGame>(m_connections, aDispatcher, this);
    Listen<MenuEvent, &SteamService::OnMenu>(m_connections, aDispatcher, this);
    Listen<ConnectedEvent, &SteamService::OnConnected>(m_connections, aDispatcher, this);
}

void SteamService::Notify(std::string aText) noexcept
{
    m_dispatcher.trigger(SessionNoticeEvent{std::move(aText)});
}

void SteamService::OnGameLoaded(const GameLoadedEvent&) noexcept
{
    HostWorld();
}

void SteamService::OnNewGame(const NewGameEvent&) noexcept
{
    m_newGame = true;
}

void SteamService::OnMenu(const MenuEvent& acEvent) noexcept
{
    if (m_newGame && !acEvent.Opening && acEvent.Menu == "Loading Menu")
    {
        m_newGame = false;
        HostWorld();
    }
}

// A world of this player's is up (a save loaded, a new game): host, unless this game is in someone else's session or
// already in a Steam one.
void SteamService::HostWorld() noexcept
{
    const bool cGuestElsewhere = m_world.GetTransport().IsOnline() && !m_world.GetHostService().IsHosting();
    // The host's save a main menu join loads is not this player's to host.
    const auto* pJoin = m_world.ctx().find<JoinService>();
    if (cGuestElsewhere || (pJoin && (pJoin->IsJoining() || pJoin->IsCreatingCharacter())) || (m_status.State != "idle" && m_status.State != "failed") || !m_steam.IsReady())
        return;
    spdlog::info("Steam: a world of this player's is up; hosting");
    Host();
}

SteamService::SteamId SteamService::JoinedAs() const noexcept
{
    const bool cJoined = !m_hosting && (m_status.State == "connecting" || m_status.State == "connected");
    return cJoined ? m_steam.GetUserId() : 0;
}

SteamService::~SteamService() noexcept
{
    Leave();
}

bool SteamService::Host() noexcept
{
    if ((m_status.State != "idle" && m_status.State != "failed") || !m_steam.IsReady())
        return false;
    // The session the lobby leads to: this game hosts it and plays in it, as the Host button does.
    auto& host = m_world.GetHostService();
    if (!host.IsHosting() && !host.StartHosting())
        return false;
    if (!m_world.GetTransport().IsOnline())
        m_world.GetTransport().ConnectLocal(*host.GetNetwork());
    if (!m_steam.CreateLobby())
        return false;
    m_status = {.State = "creating_lobby"};
    m_hosting = true;
    m_waited = 0.0;
    spdlog::info("Steam: creating a lobby");
    return true;
}

bool SteamService::Invite(SteamId aFriend) noexcept
{
    if (!m_hosting || m_status.State != "hosting")
        return false;
    spdlog::info("Steam: inviting {} to lobby {}", aFriend, m_status.Lobby);
    return m_steam.InviteToLobby(m_status.Lobby, aFriend);
}

void SteamService::Leave() noexcept
{
    // Its goodbye goes through the tunnel before the tunnel closes: the host's listener drops the player at once.
    if (m_joinedTransport)
    {
        m_world.GetTransport().Close();
        PumpTunnels();
    }
    m_joinedTransport = false;
    PumpTunnels(); // the host's final session notice too
    for (const auto& cPeer : m_status.Peers)
    {
        CloseTunnel(cPeer.Connection);
        m_steam.Close(cPeer.Connection);
    }
    m_tunnels.clear();
    if (m_hosting)
        m_steam.StopListening();
    m_steam.LeaveLobby(m_status.Lobby);
    m_status = {};
    m_hosting = false;
}

void SteamService::Fail(std::string aError) noexcept
{
    spdlog::warn("Steam: {}", aError);
    if (auto* pJoin = m_world.ctx().find<JoinService>())
        pJoin->CreationHostLost();
    // A reconnect's try: JoinService's reconnect says how it ends.
    if (m_hosting)
        Notify("Steam session: " + aError);
    else if (m_status.State == "connected" && !m_rejoining)
        Notify("Left " + m_status.HostName + "'s game: " + aError);
    else if (!m_rejoining)
        Notify("Couldn't join: " + aError);
    Leave();
    m_status.State = "failed";
    m_status.Error = std::move(aError);
}

bool SteamService::StepDown() noexcept
{
    auto& host = m_world.GetHostService();
    if (!host.IsHosting())
        return true;
    if (!m_status.Peers.empty() || (host.GetNetwork() && host.GetNetwork()->GetClientCount() != 0))
        return false;
    if (m_hosting)
        Leave();
    host.StopHosting();
    return true;
}

bool SteamService::Rejoin() noexcept
{
    const SteamId cLobby = m_rejoinLobby ? m_rejoinLobby : JoinedAs() ? m_status.Lobby : 0;
    if (!cLobby || m_hosting)
        return false;
    // What the dropped session or the last try left goes first.
    Leave();
    m_rejoinLobby = cLobby;
    m_rejoining = true;
    JoinLobby(cLobby);
    return true;
}

void SteamService::EndRejoin() noexcept
{
    if (!m_rejoining)
        return;
    Leave();
    m_rejoining = false;
    m_rejoinLobby = 0;
}

void SteamService::OnConnected(const ConnectedEvent&) noexcept
{
    m_rejoining = false;
    m_rejoinLobby = 0;
}

void SteamService::JoinLobby(SteamId aLobby) noexcept
{
    // Hosting alone (a save loaded, nobody joined): this game steps down to join its friend's.
    if (m_hosting && !StepDown())
    {
        Notify("Couldn't join: friends are in your game. Leave it first.");
        return;
    }
    if (m_status.State != "idle" && m_status.State != "failed")
    {
        spdlog::warn("Steam: a join request for lobby {} while {}; ignored", aLobby, m_status.State);
        return;
    }
    if (m_world.GetTransport().IsOnline())
        return Fail("already in a session: leave it to join another");
    if (!m_steam.JoinLobby(aLobby))
        return Fail("Steam refused to join the lobby");
    m_status = {.State = "joining_lobby", .Lobby = aLobby};
    m_hosting = false;
    m_waited = 0.0;
    spdlog::info("Steam: joining lobby {}", aLobby);
    if (!m_rejoining)
        Notify("Joining a friend's game...");
}

void SteamService::OnLobbyResult(const ISteamBridge::LobbyResult& acResult) noexcept
{
    if (!acResult.Lobby)
        return Fail(std::format("Steam could not {} the lobby (result {})", m_hosting ? "create" : "join", acResult.Reason));
    m_status.Lobby = acResult.Lobby;
    m_status.Host = m_steam.GetLobbyOwner(acResult.Lobby);
    if (m_hosting)
    {
        if (!m_steam.SetLobbyData(acResult.Lobby, kProtocolKey, kProtocol) || !m_steam.Listen())
            return Fail("Steam would not publish the lobby or listen");
        m_status.State = "hosting";
        spdlog::info("Steam: hosting lobby {}", acResult.Lobby);
        Notify("Hosting: friends can join from Steam (Shift+Tab, Invite to Game).");
        return;
    }
    if (m_steam.GetLobbyData(acResult.Lobby, kProtocolKey) != kProtocol)
        return Fail("the lobby is not a SkyrimCoop session of this version");
    const ISteamBridge::Connection cConnection = m_status.Host ? m_steam.Connect(m_status.Host) : 0;
    if (!cConnection)
        return Fail("Steam could not start a connection to the host");
    m_status.Peers.push_back({.Connection = cConnection, .Id = m_status.Host});
    m_status.State = "connecting";
    m_waited = 0.0;
    spdlog::info("Steam: connecting to the host {}", m_status.Host);
}

SteamService::Peer* SteamService::FindPeer(ISteamBridge::Connection aConnection) noexcept
{
    const auto it = std::ranges::find(m_status.Peers, aConnection, &Peer::Connection);
    return it == m_status.Peers.end() ? nullptr : &*it;
}

void SteamService::SendControl(ISteamBridge::Connection aConnection, const std::string& acLine) noexcept
{
    std::string message(1, static_cast<char>(kControl));
    message += acLine;
    m_steam.Send(aConnection, message.data(), message.size(), true);
}

void SteamService::OnControl(Peer& aPeer, const std::string& acLine) noexcept
{
    const std::string_view cHello = kHello, cWelcome = kWelcome;
    if (m_hosting && acLine.starts_with(cHello))
    {
        // Its tunnel into our listener, before the welcome lets its transport dial.
        auto pPort = std::make_unique<Net::LoopbackPort>();
        const HostNetwork* pNetwork = m_world.GetHostService().GetNetwork();
        if (!pNetwork || !pPort->Open())
        {
            spdlog::error("Steam: no tunnel for {} (hosting: {})", aPeer.Id, pNetwork != nullptr);
            m_steam.Close(aPeer.Connection);
            return;
        }
        m_tunnels[aPeer.Connection] = {.Port = std::move(pPort), .EnetPort = pNetwork->GetPort()};
        aPeer.Name = acLine.substr(cHello.size());
        SendControl(aPeer.Connection, kWelcome + m_steam.GetUserName());
        spdlog::info("Steam: {} ({}) joined, {}", aPeer.Name, aPeer.Id, aPeer.Relayed ? "relayed" : "direct");
    }
    else if (!m_hosting && acLine.starts_with(cWelcome) && m_status.State == "connecting")
    {
        m_status.HostName = aPeer.Name = acLine.substr(cWelcome.size());
        auto pPort = std::make_unique<Net::LoopbackPort>();
        if (!pPort->Open())
            return Fail("no loopback port for the session's tunnel");
        const uint16_t cPort = pPort->Port();
        m_tunnels[aPeer.Connection] = {.Port = std::move(pPort)};
        // The whole join (the host's save, this player's character) runs through the tunnel.
        auto* pJoin = m_world.ctx().find<JoinService>();
        const std::string cEndpoint = std::format("127.0.0.1:{}", cPort);
        const bool cDialled = m_rejoining && pJoin ? pJoin->Redial(cEndpoint)
                              : pJoin       ? pJoin->Join(cEndpoint)
                                            : m_world.GetTransport().Connect(cEndpoint);
        if (!cDialled)
            return Fail("the transport would not dial the tunnel");
        m_joinedTransport = true;
        m_status.State = "connected";
        spdlog::info("Steam: connected to {}'s session, {}; joining it through the tunnel", aPeer.Name,
                     aPeer.Relayed ? "relayed" : "direct");
        if (!m_rejoining)
            Notify("Joined " + aPeer.Name + "'s game.");
    }
    else
        spdlog::warn("Steam: unexpected control line from {} ({} bytes)", aPeer.Id, acLine.size());
}

void SteamService::CloseTunnel(ISteamBridge::Connection aConnection) noexcept
{
    const auto it = m_tunnels.find(aConnection);
    if (it == m_tunnels.end())
        return;
    HostNetwork* pNetwork = m_hosting ? m_world.GetHostService().GetNetwork() : nullptr;
    if (const auto cSession = pNetwork ? pNetwork->FindLocalConnection(it->second.Port->Port()) : 0)
        pNetwork->Kick(cSession);
    m_tunnels.erase(it);
}

// Datagrams each way: the enet host behind our end of each tunnel, and its Steam connection.
void SteamService::PumpTunnels() noexcept
{
    for (auto& [cConnection, tunnel] : m_tunnels)
    {
        for (auto& datagram : tunnel.Port->Receive())
        {
            if (!m_hosting)
                tunnel.EnetPort = datagram.From; // the friend's transport, which dialled us
            datagram.Data.insert(datagram.Data.begin(), static_cast<uint8_t>(kDatagram));
            m_steam.Send(cConnection, datagram.Data.data(), datagram.Data.size(), false);
        }
    }
}

void SteamService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    const ISteamBridge::Events cEvents = m_steam.Poll();
    for (const SteamId cLobby : cEvents.JoinRequests)
        JoinLobby(cLobby);
    // Started by Steam for a friend's invite (+connect_lobby): that lobby, once the main menu is up.
    if (!m_launchLobbyTaken && m_steam.IsReady())
        if (m_world.ctx().contains<IJoinBridge&>() && m_world.ctx().at<IJoinBridge&>().IsAtMainMenu())
        {
            m_launchLobbyTaken = true;
            if (const SteamId cLobby = m_steam.GetLaunchLobby())
            {
                spdlog::info("Steam: started for lobby {}", cLobby);
                JoinLobby(cLobby);
            }
        }

    m_waited += acEvent.Delta;
    if (m_status.State == "creating_lobby" || m_status.State == "joining_lobby")
    {
        if (const auto cResult = m_steam.PollLobbyRequest(); cResult.Done)
            OnLobbyResult(cResult);
        else if (m_waited > kTimeoutSeconds)
            return Fail("Steam did not answer the lobby request");
    }

    for (const auto cConnection : cEvents.Incoming)
    {
        if (!m_hosting || m_status.State != "hosting" || !m_steam.Accept(cConnection))
        {
            m_steam.Close(cConnection);
            continue;
        }
        m_status.Peers.push_back({.Connection = cConnection, .Id = m_steam.GetConnectionInfo(cConnection).Peer});
    }
    for (const auto cConnection : cEvents.Connected)
    {
        Peer* pPeer = FindPeer(cConnection);
        if (!pPeer)
            continue;
        pPeer->Relayed = m_steam.GetConnectionInfo(cConnection).Relayed;
        if (!m_hosting)
            SendControl(cConnection, kHello + m_steam.GetUserName());
    }
    for (size_t i = 0; i < m_status.Peers.size(); ++i)
    {
        const auto cConnection = m_status.Peers[i].Connection;
        for (const auto& cMessage : m_steam.Receive(cConnection))
        {
            if (cMessage.empty())
                continue;
            if (cMessage[0] == kControl)
                OnControl(m_status.Peers[i], std::string(cMessage.begin() + 1, cMessage.end()));
            else if (const auto it = m_tunnels.find(cConnection); cMessage[0] == kDatagram && it != m_tunnels.end() && it->second.EnetPort)
                it->second.Port->SendTo(it->second.EnetPort, cMessage.data() + 1, cMessage.size() - 1);
            if (m_status.Peers.size() <= i) // a failure above left the session
                return;
        }
    }
    PumpTunnels();

    // After their last messages: Steam delivers what a closing peer sent before it reports the close.
    for (const auto& [cConnection, cReason] : cEvents.Closed)
    {
        if (!FindPeer(cConnection))
            continue;
        if (!m_hosting && m_joinedTransport && m_world.GetTransport().IsOnline())
        {
            // The session dropped: JoinService reconnects (Rejoin), and the chat says so.
            spdlog::warn("Steam: lost the host ({}); reconnecting", cReason);
            m_rejoinLobby = m_status.Lobby;
            m_joinedTransport = false;
            m_world.GetTransport().Close(DisconnectedEvent::Cause::kLost);
            return;
        }
        if (!m_hosting)
            return Fail("lost the host: " + cReason);
        spdlog::info("Steam: a guest left ({})", cReason);
        CloseTunnel(cConnection);
        std::erase_if(m_status.Peers, [cConnection](const Peer& acPeer) { return acPeer.Connection == cConnection; });
    }

    if (m_status.State == "connecting" && m_waited > kTimeoutSeconds)
        Fail("the host did not answer");
}
