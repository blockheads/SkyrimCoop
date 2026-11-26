#pragma once

#include <string_view>

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

/// Steam as Skyrim's own steam_api64 offers it: the signed-in account, lobbies (ISteamMatchmaking) and P2P connections
/// relayed through Valve's network (ISteamNetworkingSockets). No Steam types: ids are SteamID64s, connections Steam's
/// handles. docs/design/join-flow.md, "Steam".
class ISteamBridge
{
public:
    using SteamId = uint64_t;
    using Connection = uint32_t;

    /// A lobby request's answer (LobbyCreated_t, LobbyEnter_t): the lobby, or none and Steam's reason.
    struct LobbyResult
    {
        bool Done{};
        SteamId Lobby{};
        int Reason{};
    };

    /// What Steam told this game since the last Poll, in order per kind.
    struct Events
    {
        /// The player accepted an invite to a lobby or chose "Join Game" on a friend in one (GameLobbyJoinRequested_t).
        std::vector<SteamId> JoinRequests;
        /// A peer connecting to our listen socket, not accepted yet.
        std::vector<Connection> Incoming;
        std::vector<Connection> Connected;
        /// Closed by the peer or lost, with Steam's description.
        std::vector<std::pair<Connection, std::string>> Closed;
    };

    struct ConnectionInfo
    {
        SteamId Peer{};
        bool Relayed{}; // through Valve's relays rather than a direct route
        std::string Description;
    };

    virtual ~ISteamBridge() = default;

    /// "+connect_lobby <id>" in a command line, else 0.
    [[nodiscard]] static SteamId ParseConnectLobby(std::string_view aCommandLine) noexcept
    {
        constexpr std::string_view cFlag = "+connect_lobby";
        const size_t cAt = aCommandLine.find(cFlag);
        if (cAt == std::string_view::npos)
            return 0;
        size_t i = aCommandLine.find_first_not_of(" \t\"", cAt + cFlag.size());
        SteamId id = 0;
        for (; i < aCommandLine.size() && aCommandLine[i] >= '0' && aCommandLine[i] <= '9'; ++i)
            id = id * 10 + static_cast<SteamId>(aCommandLine[i] - '0');
        return id;
    }

    /// Signed in, under Skyrim's App ID; until then every other call fails.
    [[nodiscard]] virtual bool IsReady() noexcept = 0;
    [[nodiscard]] virtual SteamId GetUserId() noexcept = 0;
    [[nodiscard]] virtual std::string GetUserName() noexcept = 0;
    /// The lobby Steam started this game to join (a friend's invite accepted with the game closed: "+connect_lobby
    /// <id>" on its command line), else 0.
    [[nodiscard]] virtual SteamId GetLaunchLobby() noexcept = 0;
    /// Runs the networking callbacks and hands over what arrived.
    virtual Events Poll() noexcept = 0;

    // Lobbies, one request at a time.

    /// Friends only: friends can join from their friends list too, others only by invite.
    virtual bool CreateLobby() noexcept = 0;
    virtual bool JoinLobby(SteamId aLobby) noexcept = 0;
    virtual LobbyResult PollLobbyRequest() noexcept = 0;
    [[nodiscard]] virtual SteamId GetLobbyOwner(SteamId aLobby) noexcept = 0;
    virtual bool SetLobbyData(SteamId aLobby, const std::string& acKey, const std::string& acValue) noexcept = 0;
    [[nodiscard]] virtual std::string GetLobbyData(SteamId aLobby, const std::string& acKey) noexcept = 0;
    virtual bool InviteToLobby(SteamId aLobby, SteamId aFriend) noexcept = 0;
    virtual void LeaveLobby(SteamId aLobby) noexcept = 0;

    // P2P connections.

    virtual bool Listen() noexcept = 0;
    virtual void StopListening() noexcept = 0;
    /// 0 when Steam refuses at once; otherwise it reports Connected or Closed later.
    virtual Connection Connect(SteamId aPeer) noexcept = 0;
    virtual bool Accept(Connection aConnection) noexcept = 0;
    /// Unreliable goes at once (no Nagle delay): it carries enet datagrams, which keep their own reliability.
    virtual bool Send(Connection aConnection, const void* apData, size_t aSize, bool aReliable) noexcept = 0;
    virtual std::vector<std::vector<uint8_t>> Receive(Connection aConnection) noexcept = 0;
    virtual void Close(Connection aConnection) noexcept = 0;
    [[nodiscard]] virtual ConnectionInfo GetConnectionInfo(Connection aConnection) noexcept = 0;
};
