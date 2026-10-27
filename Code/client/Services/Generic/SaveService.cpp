#include <Services/SaveService.h>

#include <Events/ConnectedEvent.h>
#include <Events/GameSavingEvent.h>
#include <Events/SessionNoticeEvent.h>
#include <Services/HostService.h>
#include <Services/JoinService.h>
#include <Services/TransportService.h>
#include <World.h>

SaveService::SaveService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
    , m_dispatcher(aDispatcher)
{
    Listen<ConnectedEvent, &SaveService::OnConnected>(m_connections, aDispatcher, this);
}

bool SaveService::MaySave() const noexcept
{
    auto& transport = m_world.GetTransport();
    const bool cGuest = transport.IsOnline() && !m_world.GetHostService().IsHosting();
    const auto* pJoin = m_world.ctx().find<JoinService>();
    return !cGuest && !(pJoin && pJoin->IsJoining());
}

bool SaveService::OnSaveRequested(bool aAutosave) noexcept
{
    if (MaySave())
        return true;
    spdlog::info("SaveService: {} refused, this game is a guest", aAutosave ? "an autosave" : "a save");
    if (!aAutosave || !m_toldAutosave)
        m_dispatcher.trigger(SessionNoticeEvent{kRefusal});
    m_toldAutosave |= aAutosave;
    return false;
}

bool SaveService::OnLoadRequested(const std::string& acName) noexcept
{
    if (!MaySave())
    {
        m_dispatcher.trigger(SessionNoticeEvent{kLoadRefusal});
        return false;
    }
    auto& host = m_world.GetHostService();
    if (!host.IsHosting())
        return true;
    host.LoadSave(acName);
    return false;
}

std::string SaveService::WriteCoSave() noexcept
{
    m_dispatcher.trigger(GameSavingEvent{});
    spdlog::info("SaveService: {} player record(s) into the co-save", m_records.All().size());
    return m_records.Encode();
}

void SaveService::ReadCoSave(std::string_view aData, uint32_t aVersion) noexcept
{
    m_records.Decode(aData, aVersion);
    spdlog::info("SaveService: {} player record(s) from the co-save", m_records.All().size());
}

void SaveService::RevertCoSave() noexcept
{
    m_records.Clear();
}
