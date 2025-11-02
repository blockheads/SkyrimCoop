#include <GameServer.h>

#include <World.h>
#include <Services/AdminService.h>

#include <AdminMessages/AdminShutdownRequest.h>
#include <AdminMessages/ServerLogs.h>

namespace Server
{

AdminService::AdminService(World& aWorld, entt::dispatcher& aDispatcher)
    : m_world(aWorld)
{
    m_shutdownConnection = aDispatcher.sink<AdminPacketEvent<AdminShutdownRequest>>().connect<&AdminService::HandleShutdown>(this);
}

void AdminService::HandleShutdown(const AdminPacketEvent<AdminShutdownRequest>& acMessage) noexcept
{
    spdlog::warn("Shutdown was requested by {:x}", acMessage.ConnectionId);

    GameServer::Get()->Kill();
}

void AdminService::sink_it_(const spdlog::details::log_msg& msg)
{
    // Check if GameServer still exists before trying to send logs
    auto* pServer = GameServer::Get();
    if (!pServer)
        return;

    spdlog::memory_buf_t formatted;
    formatter_->format(msg, formatted);

    ServerLogs logs;
    logs.Logs = fmt::to_string(formatted);

    pServer->ForEachAdmin([&logs, pServer](ConnectionId_t aId) { pServer->Send(aId, logs); });
}

void AdminService::flush_()
{
}

} // namespace Server
