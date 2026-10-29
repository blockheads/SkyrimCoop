#pragma once

#include <Events/Listen.h>
#include <NoCopyMove.h>
#include <Messages/RelayedMessages.h>

#include <string>

struct World;
struct TransportService;
class IBugReportBridge;

/**
 * @brief A player's /bug (docs/design/bug-reports.md): sent to the host, which names it and sends it to every game;
 * each writes its own part (IBugReportBridge). Alone, this game writes its part at once.
 */
struct BugReportService
{
    BugReportService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport, IBugReportBridge& aBridge) noexcept;
    ~BugReportService() noexcept = default;

    NO_COPY_MOVE(BugReportService);

    void Report(const std::string& acText) noexcept;
    // This game's files since the last export, in one zip to send (/export).
    void Export() noexcept;

protected:
    void OnNotifyBugReport(const NotifyBugReport& acMessage) noexcept;

private:
    void Write(const Payloads::BugReport& acReport) noexcept;

    World& m_world;
    TransportService& m_transport;
    IBugReportBridge& m_bridge;

    std::vector<entt::scoped_connection> m_connections;
};
