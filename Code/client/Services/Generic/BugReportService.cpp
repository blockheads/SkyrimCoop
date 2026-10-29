#include <Services/BugReportService.h>
#include <Services/HostService.h>
#include <Services/TransportService.h>

#include <Bridges/IBugReportBridge.h>
#include <Diagnostics/ReportPaths.h>
#include <Events/SessionNoticeEvent.h>
#include <Messages/Relayed.h>
#include <Services/RunnerService.h>
#include <World.h>

BugReportService::BugReportService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport,
                                   IBugReportBridge& aBridge) noexcept
    : m_world(aWorld)
    , m_transport(aTransport)
    , m_bridge(aBridge)
{
    Listen<NotifyBugReport, &BugReportService::OnNotifyBugReport>(m_connections, aDispatcher, this);
}

void BugReportService::Report(const std::string& acText) noexcept
{
    BugReportRequest request;
    request.Text = acText;
    if (m_transport.Send(request))
        return;
    // Not in a session: this game's part alone.
    request.Id = Diagnostics::NewReportId();
    Write(request);
}

void BugReportService::OnNotifyBugReport(const NotifyBugReport& acMessage) noexcept
{
    Write(acMessage);
}

void BugReportService::Write(const Payloads::BugReport& acReport) noexcept
{
    const BugReportPart cPart{acReport.Id, acReport.Text, acReport.By, acReport.SessionTick, m_world.GetHostService().IsHosting(), m_transport.GetLocalPlayerName()};
    const std::string cFolder = m_bridge.Write(cPart);
    m_world.GetDispatcher().trigger(SessionNoticeEvent{std::format("Bug report {} saved: {}", acReport.Id, cFolder)});
}

void BugReportService::Export() noexcept
{
    m_world.GetDispatcher().trigger(SessionNoticeEvent{"Exporting reports and logs..."});
    RunnerService& runner = m_world.GetRunner();
    m_bridge.Export(m_transport.GetLocalPlayerName(), [&runner](const std::string& acZip) {
        runner.Trigger(SessionNoticeEvent{acZip.empty() ? "Export failed (see the log)" : std::format("Saved {} (path copied)", acZip)});
    });
}
