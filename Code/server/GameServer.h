#pragma once

#include <AdminMessages/Message.h>
#include <Messages/AuthenticationRequest.h>
#include <Messages/Message.h>
#include <Server.hpp>
#include <World.h>
#include <memory>

using TiltedPhoques::ConnectionId_t;
// Removed: using TiltedPhoques::Server;  -- Conflicts with namespace Server
using TiltedPhoques::String;

class EnetRecorder;
struct AuthenticationRequest;
struct QuestLog;
struct Player;

namespace Resources
{
struct ResourceCollection;
}

namespace ServerConsole
{
class ConsoleRegistry;
}

// Tag struct for constructing GameServer without port binding (test/replay mode)
struct GameServerTestMode {};

namespace Server
{

struct GameServer final : TiltedPhoques::Server
{
    // TODO: eventually refactor this.
    struct Info
    {
        String name;
        String desc;
        String icon_url;
        String tagList;
        uint16_t tick_rate;
    };

    GameServer(ServerConsole::ConsoleRegistry& aConsole) noexcept;
    GameServer(ServerConsole::ConsoleRegistry& aConsole, GameServerTestMode) noexcept;
    virtual ~GameServer();

    TP_NOCOPYMOVE(GameServer);

    static GameServer* Get() noexcept;

    void Initialize();
    void Kill();

    void BindMessageHandlers();
    void BindServerCommands();


    // Loopback support for P2P host mode (in-process message delivery without enet6)
    void InjectMessage(const void* apData, uint32_t aSize, ConnectionId_t aConnectionId);
    // A connection ending without the network (tests).
    void InjectDisconnection(ConnectionId_t aConnectionId) { OnDisconnection(aConnectionId, EDisconnectReason::Quit); }
    void SetLoopbackRoute(ConnectionId_t aConnectionId, std::function<void(const void*, uint32_t)> aCallback);
    // The host's bridge (client HostBridge) sees every message first; true: it handled it.
    using FrontDoor = std::function<bool(const ClientMessage&, ConnectionId_t)>;
    void SetFrontDoor(FrontDoor aFrontDoor) noexcept { m_frontDoor = std::move(aFrontDoor); }
    // The connection of the game in this process (the host's), 0 without one.
    [[nodiscard]] ConnectionId_t LoopbackConnection() const noexcept { return m_loopbackConnectionId; }
    // The server's own record of a player, for its last services (quests, admin, Lua) until they go.
    void AddPlayer(ConnectionId_t aConnectionId, const AuthenticationRequest& acRequest) noexcept;
    // Ends a connection that is not a player (a refused join).
    void Disconnect(ConnectionId_t aConnectionId) noexcept;
    // The player's own character, for the player list and the services still here (quests).
    void SetPlayerCharacter(ConnectionId_t aConnectionId, uint32_t aServerId, const QuestLog& acQuests) noexcept;
    // Character ids share the World's entity ids with objects (one id space while both exist).
    [[nodiscard]] uint32_t NewCharacterId() noexcept;
    void FreeCharacterId(uint32_t aServerId) noexcept;
    // A connection ended, a player's or not (the bridge decides what that means), before the
    // server drops its own record.
    using DisconnectionHandler = std::function<void(ConnectionId_t aConnection)>;
    void SetDisconnectionHandler(DisconnectionHandler aHandler) noexcept { m_disconnectionHandler = std::move(aHandler); }
    // The host's own client connecting in-process (replies go to aCallback) and leaving again.
    void OpenLoopback(ConnectionId_t aConnectionId, std::function<void(const void*, uint32_t)> aCallback);
    void CloseLoopback();
    // Test mode: intercept ALL outgoing messages regardless of peer (for multi-peer replay tests)
    void SetTestSendCallback(std::function<void(ConnectionId_t, const void*, uint32_t)> aCallback);

    // Recording taps for enet6 traffic replay
    void StartRecording(const std::string& aOutputDir);
    void StopRecording();

    // Packet dispatching
    void Send(ConnectionId_t aConnectionId, const ServerMessage& acServerMessage) const;
    void Send(ConnectionId_t aConnectionId, const ServerMessage& acServerMessage,
              TiltedPhoques::EPacketFlags aFlags) const;
    void Send(ConnectionId_t aConnectionId, const ServerAdminMessage& acServerMessage) const;
    void SendToLoaded(const ServerMessage& acServerMessage) const;
    void SendToPlayers(const ServerMessage& acServerMessage,
                       const Player* apExcludeSender = nullptr) const; // Primary broadcast method for P2P co-op

    const Info& GetInfo() const noexcept
    {
        return m_info;
    }

    bool IsRunning() const noexcept
    {
        return !m_requestStop;
    }
    template <class T> void ForEachAdmin(const T& aFunctor)
    {
        for (auto id : m_adminSessions)
            aFunctor(id);
    }

    struct Uptime
    {
        int weeks;
        int days;
        int hours;
        int minutes;
    };
    Uptime GetUptime() const noexcept;

    World& GetWorld() const noexcept
    {
        return *m_pWorld;
    }

    [[nodiscard]] const TiltedPhoques::Set<ConnectionId_t>& GetAdminSessions() const noexcept
    {
        return m_adminSessions;
    }

    void AddAdminSession(ConnectionId_t acSession) noexcept
    {
        m_adminSessions.insert(acSession);
    }

    void RemoveAdminSession(ConnectionId_t acSession) noexcept
    {
        m_adminSessions.erase(acSession);
    }

    Player* GetAdminByUsername(const String& acUsername) const noexcept;
    Player const* GetAdminByUsername(const String& acUsername) noexcept;

  protected:
    // Implement TiltedPhoques::Server
    void OnUpdate() override;
    void OnConsume(const void* apData, uint32_t aSize, ConnectionId_t aConnectionId) override;
    void OnConnection(ConnectionId_t aHandle) override;
    void OnDisconnection(ConnectionId_t aConnectionId, EDisconnectReason aReason) override;

  private:
    void UpdateTitle() const;
    String SanitizeUsername(const String& acUsername) const noexcept;

  private:
    std::chrono::high_resolution_clock::time_point m_startTime;
    std::chrono::high_resolution_clock::time_point m_lastFrameTime;
    std::function<void(UniquePtr<ClientMessage>&, ConnectionId_t)> m_messageHandlers[kClientOpcodeMax];
    std::function<void(UniquePtr<ClientAdminMessage>&, ConnectionId_t)> m_adminMessageHandlers[kClientAdminOpcodeMax];

    Info m_info{};
    UniquePtr<Resources::ResourceCollection> m_pResources;
    ServerConsole::ConsoleRegistry& m_commands;

    TiltedPhoques::Set<ConnectionId_t> m_adminSessions;
    TiltedPhoques::Map<ConnectionId_t, entt::entity> m_connectionToEntity;

    UniquePtr<World> m_pWorld;
    std::unique_ptr<EnetRecorder> m_pRecorder;

    bool m_requestStop;

    // Loopback routing for P2P host mode
    ConnectionId_t m_loopbackConnectionId{0};
    DisconnectionHandler m_disconnectionHandler;
    FrontDoor m_frontDoor;
    std::function<void(const void*, uint32_t)> m_loopbackCallback;
    // Test mode: catch-all send callback for multi-peer replay
    std::function<void(ConnectionId_t, const void*, uint32_t)> m_testSendCallback;

    static inline GameServer* s_pInstance = nullptr;
};

} // namespace Server
