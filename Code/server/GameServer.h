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
struct Player;
struct PartyComponent;

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

    bool CheckMoPo();
    void BindMessageHandlers();
    void BindServerCommands();

    void UpdateTimeScale();
    void UpdateSettings();
    void EnforceProximity(); // P2P co-op: Teleport players back if they wander too far from host

    // Loopback support for P2P host mode (in-process message delivery without enet6)
    void InjectMessage(const void* apData, uint32_t aSize, ConnectionId_t aConnectionId);
    void SetLoopbackRoute(ConnectionId_t aConnectionId, std::function<void(const void*, uint32_t)> aCallback);
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
    void SendToParty(const ServerMessage& acServerMessage, const PartyComponent& acPartyComponent,
                     const Player* apExcludeSender = nullptr) const;

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
    bool ValidateAuthParams(ConnectionId_t aConnectionId, const UniquePtr<AuthenticationRequest>& acRequest);
    void HandleAuthenticationRequest(ConnectionId_t aConnectionId, const UniquePtr<AuthenticationRequest>& acRequest);

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
    std::function<void(const void*, uint32_t)> m_loopbackCallback;
    // Test mode: catch-all send callback for multi-peer replay
    std::function<void(ConnectionId_t, const void*, uint32_t)> m_testSendCallback;

    static inline GameServer* s_pInstance = nullptr;
};

} // namespace Server
