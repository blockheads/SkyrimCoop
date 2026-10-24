#pragma once

#include <Bridges/ISteamBridge.h>
#include <Events/Listen.h>
#include <NoCopyMove.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Net
{
struct LoopbackPort;
}
struct GameLoadedEvent;
struct UpdateEvent;
struct World;

/// A Steam session (docs/design/join-flow.md, "Steam"): the host's game makes a friends-only lobby and listens for P2P
/// connections; a friend's game joins the lobby when its player accepts an invite or picks "Join Game", and connects
/// to the lobby's owner through Valve's relays. The session itself is enet tunnelled through that connection: the
/// friend's transport dials a loopback port, and the host's tunnel delivers into its own listener. Every game runs it;
/// who hosts is whoever made the lobby. A game that loads a save hosts by itself (friends join through Steam's invite
/// or "Join Game"); one hosting alone steps down to join a friend's.
struct SteamService
{
    using SteamId = ISteamBridge::SteamId;

    struct Peer
    {
        ISteamBridge::Connection Connection{};
        SteamId Id{};
        std::string Name; // from its hello; empty until then
        bool Relayed{};
    };

    struct Status
    {
        std::string State{"idle"}; // idle, creating_lobby, hosting, joining_lobby, connecting, connected, failed
        std::string Error;
        SteamId Lobby{};
        SteamId Host{};
        std::string HostName; // a guest's: from the host's welcome
        std::vector<Peer> Peers; // the host's: guests connected; a guest's: the host
    };

    SteamService(World& aWorld, entt::dispatcher& aDispatcher, ISteamBridge& aSteam) noexcept;
    ~SteamService() noexcept;

    NO_COPY_MOVE(SteamService);

    /// Hosts the game's session (if it does not yet), makes the lobby and listens; false when Steam is not ready or a
    /// Steam session is under way.
    bool Host() noexcept;
    /// The host's: invites a friend to its lobby (Steam shows them the invite).
    bool Invite(SteamId aFriend) noexcept;
    void Leave() noexcept;
    [[nodiscard]] const Status& GetStatus() const noexcept { return m_status; }

    // Each Steam message starts with its kind: a control line (reliable) or a tunnelled enet datagram (unreliable).
    enum Kind : uint8_t
    {
        kControl = 0,
        kDatagram = 1,
    };
    // The first control line each way on a connection; the version must match.
    static constexpr const char* kHello = "SkyrimCoop/1 hello ";
    static constexpr const char* kWelcome = "SkyrimCoop/1 welcome ";
    static constexpr const char* kProtocolKey = "skyrimcoop_protocol";
    static constexpr const char* kProtocol = "1";
    static constexpr double kTimeoutSeconds = 30.0;

private:
    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    void OnGameLoaded(const GameLoadedEvent&) noexcept;
    void Notify(std::string aText) noexcept;
    void Fail(std::string aError) noexcept;
    void JoinLobby(SteamId aLobby) noexcept;
    void OnLobbyResult(const ISteamBridge::LobbyResult& acResult) noexcept;
    void OnControl(Peer& aPeer, const std::string& acLine) noexcept;
    void SendControl(ISteamBridge::Connection aConnection, const std::string& acLine) noexcept;
    void PumpTunnels() noexcept;
    // The host's: a friend's tunnel goes, and with it the session's connection behind it (no wait for enet's timeout).
    void CloseTunnel(ISteamBridge::Connection aConnection) noexcept;
    Peer* FindPeer(ISteamBridge::Connection aConnection) noexcept;

    // One end of the tunnel for each Steam connection: the host's sends to its listener, a friend's to its transport.
    struct Tunnel
    {
        std::unique_ptr<Net::LoopbackPort> Port;
        uint16_t EnetPort{}; // the enet host behind it: the host's listener, or the friend's client once it sent
    };

    World& m_world;
    entt::dispatcher& m_dispatcher;
    ISteamBridge& m_steam;
    std::map<ISteamBridge::Connection, Tunnel> m_tunnels;
    bool m_joinedTransport{}; // a friend's transport dialled our tunnel
    Status m_status;
    bool m_hosting{};
    double m_waited{}; // seconds in the current request
    std::vector<entt::scoped_connection> m_connections;
};
