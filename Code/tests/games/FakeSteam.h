#pragma once

// Steam for games in this process, behind ISteamBridge: every game's account signed into one Steam network, its
// lobbies and its P2P connections. As Steam does, a lobby request answers on a later frame and a connection reaches
// Connected once the listener accepts it.
#include <Bridges/ISteamBridge.h>

#include <deque>
#include <map>
#include <optional>
#include <set>

struct FakeSteam final : ISteamBridge
{
    struct Lobby
    {
        SteamId Owner{};
        std::map<std::string, std::string> Data;
        std::set<SteamId> Members;
    };

    struct Link
    {
        FakeSteam* pPeer{};
        Connection PeerConnection{};
        bool Accepted{};
        std::deque<std::vector<uint8_t>> Inbox;
    };

    SteamId Id{NextId()};
    std::string Name;
    bool Ready{true};
    SteamId LaunchLobby{}; // Steam started this game with +connect_lobby <id>
    bool Listening{};
    std::vector<std::pair<SteamId, SteamId>> Invites; // (lobby, from), as Steam shows them to this player
    std::map<Connection, Link> Links;

    FakeSteam() { Users()[Id] = this; }
    ~FakeSteam() override
    {
        for (auto& [cConnection, link] : Links)
            if (link.pPeer)
                link.pPeer->PeerClosed(link.PeerConnection, "the peer's game closed");
        Users().erase(Id);
    }

    // The player accepts an invite, or picks "Join Game" on a friend in a lobby (GameLobbyJoinRequested_t).
    void ChooseJoin(SteamId aLobby) { m_pending.JoinRequests.push_back(aLobby); }
    // The network between this end and its peer fails: each end gets its close.
    void Drop(Connection aConnection)
    {
        const auto it = Links.find(aConnection);
        if (it == Links.end() || !it->second.pPeer)
            return;
        it->second.pPeer->PeerClosed(it->second.PeerConnection, "dropped");
        PeerClosed(aConnection, "dropped");
    }

    static std::map<SteamId, Lobby>& Lobbies()
    {
        static std::map<SteamId, Lobby> s_lobbies;
        return s_lobbies;
    }

    bool IsReady() noexcept override { return Ready; }
    SteamId GetLaunchLobby() noexcept override { return LaunchLobby; }
    SteamId GetUserId() noexcept override { return Id; }
    std::string GetUserName() noexcept override { return Name; }
    Events Poll() noexcept override { return std::exchange(m_pending, {}); }

    bool CreateLobby() noexcept override
    {
        if (m_request)
            return false;
        const SteamId cLobby = NextId();
        Lobbies()[cLobby] = {.Owner = Id, .Members = {Id}};
        m_request = {.Done = true, .Lobby = cLobby, .Reason = 1};
        return true;
    }
    bool JoinLobby(SteamId aLobby) noexcept override
    {
        if (m_request)
            return false;
        const auto it = Lobbies().find(aLobby);
        if (it != Lobbies().end())
            it->second.Members.insert(Id);
        m_request = it == Lobbies().end() ? LobbyResult{.Done = true, .Reason = 2} : LobbyResult{.Done = true, .Lobby = aLobby, .Reason = 1};
        return true;
    }
    LobbyResult PollLobbyRequest() noexcept override
    {
        // Steam answers on a later frame: the first poll after the request finds it pending.
        if (!m_request || m_requestFrames++ == 0)
            return {};
        m_requestFrames = 0;
        return *std::exchange(m_request, std::nullopt);
    }
    SteamId GetLobbyOwner(SteamId aLobby) noexcept override
    {
        const auto it = Lobbies().find(aLobby);
        return it == Lobbies().end() ? 0 : it->second.Owner;
    }
    bool SetLobbyData(SteamId aLobby, const std::string& acKey, const std::string& acValue) noexcept override
    {
        const auto it = Lobbies().find(aLobby);
        if (it == Lobbies().end() || it->second.Owner != Id)
            return false;
        it->second.Data[acKey] = acValue;
        return true;
    }
    std::string GetLobbyData(SteamId aLobby, const std::string& acKey) noexcept override
    {
        const auto it = Lobbies().find(aLobby);
        return it == Lobbies().end() || !it->second.Data.contains(acKey) ? "" : it->second.Data.at(acKey);
    }
    bool InviteToLobby(SteamId aLobby, SteamId aFriend) noexcept override
    {
        const auto it = Users().find(aFriend);
        if (it == Users().end() || !Lobbies().contains(aLobby))
            return false;
        it->second->Invites.emplace_back(aLobby, Id);
        return true;
    }
    void LeaveLobby(SteamId aLobby) noexcept override
    {
        const auto it = Lobbies().find(aLobby);
        if (it == Lobbies().end())
            return;
        it->second.Members.erase(Id);
        if (it->second.Members.empty())
            Lobbies().erase(it);
        else if (it->second.Owner == Id)
            it->second.Owner = *it->second.Members.begin();
    }

    bool Listen() noexcept override { return Listening = Ready; }
    void StopListening() noexcept override { Listening = false; }
    Connection Connect(SteamId aPeer) noexcept override
    {
        const Connection cMine = NextConnection();
        const auto it = Users().find(aPeer);
        if (it == Users().end() || !it->second->Listening)
        {
            m_pending.Closed.emplace_back(cMine, "no one listening");
            return cMine;
        }
        FakeSteam& peer = *it->second;
        const Connection cTheirs = NextConnection();
        Links[cMine] = {.pPeer = &peer, .PeerConnection = cTheirs};
        peer.Links[cTheirs] = {.pPeer = this, .PeerConnection = cMine};
        peer.m_pending.Incoming.push_back(cTheirs);
        return cMine;
    }
    bool Accept(Connection aConnection) noexcept override
    {
        const auto it = Links.find(aConnection);
        if (it == Links.end() || !it->second.pPeer)
            return false;
        it->second.Accepted = true;
        it->second.pPeer->Links[it->second.PeerConnection].Accepted = true;
        m_pending.Connected.push_back(aConnection);
        it->second.pPeer->m_pending.Connected.push_back(it->second.PeerConnection);
        return true;
    }
    bool Send(Connection aConnection, const void* apData, size_t aSize, bool) noexcept override
    {
        const auto it = Links.find(aConnection);
        if (it == Links.end() || !it->second.Accepted || !it->second.pPeer)
            return false;
        const auto* pBytes = static_cast<const uint8_t*>(apData);
        it->second.pPeer->Links[it->second.PeerConnection].Inbox.emplace_back(pBytes, pBytes + aSize);
        return true;
    }
    std::vector<std::vector<uint8_t>> Receive(Connection aConnection) noexcept override
    {
        const auto it = Links.find(aConnection);
        if (it == Links.end())
            return {};
        std::vector<std::vector<uint8_t>> messages(it->second.Inbox.begin(), it->second.Inbox.end());
        it->second.Inbox.clear();
        if (!it->second.pPeer) // closed by the peer: read out now
            Links.erase(it);
        return messages;
    }
    void Close(Connection aConnection) noexcept override
    {
        const auto it = Links.find(aConnection);
        if (it == Links.end())
            return;
        if (it->second.pPeer)
            it->second.pPeer->PeerClosed(it->second.PeerConnection, "closed by peer");
        Links.erase(it);
    }
    ConnectionInfo GetConnectionInfo(Connection aConnection) noexcept override
    {
        const auto it = Links.find(aConnection);
        if (it == Links.end() || !it->second.pPeer)
            return {};
        return {.Peer = it->second.pPeer->Id, .Relayed = true, .Description = "fake relay"};
    }

private:
    static std::map<SteamId, FakeSteam*>& Users()
    {
        static std::map<SteamId, FakeSteam*> s_users;
        return s_users;
    }
    static SteamId NextId()
    {
        static SteamId s_next = 76561190000000000ull;
        return ++s_next;
    }
    static Connection NextConnection()
    {
        static Connection s_next = 0;
        return ++s_next;
    }
    // What the peer sent before closing stays readable, as Steam's lingering close delivers it.
    void PeerClosed(Connection aConnection, const std::string& acReason)
    {
        const auto it = Links.find(aConnection);
        if (it == Links.end())
            return;
        it->second.pPeer = nullptr;
        it->second.Accepted = false;
        m_pending.Closed.emplace_back(aConnection, acReason);
    }

    Events m_pending;
    std::optional<LobbyResult> m_request;
    int m_requestFrames{};
};
