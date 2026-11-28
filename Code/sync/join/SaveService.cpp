#include <join/SaveService.h>

#include <core/ConnectedEvent.h>
#include <join/GameSavingEvent.h>
#include <join/IJoinBridge.h>
#include <join/SaveNames.h>
#include <core/SessionNoticeEvent.h>
#include <core/Hosting.h>
#include <join/JoinService.h>
#include <core/TransportService.h>
#include <core/World.h>

SaveService::SaveService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
    , m_dispatcher(aDispatcher)
{
    Listen<ConnectedEvent, &SaveService::OnConnected>(m_connections, aDispatcher, this);
}

bool SaveService::MaySave() const noexcept
{
    auto& transport = m_world.GetTransport();
    const bool cGuest = transport.IsOnline() && !m_world.IsHosting();
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
    // In a game only its own playthrough loads, as the Journal lists (SaveList).
    auto& bridge = m_world.ctx().at<IJoinBridge&>();
    if (!bridge.IsAtMainMenu() && SaveNames::CharacterId(acName) != bridge.CurrentCharacter())
    {
        m_dispatcher.trigger(SessionNoticeEvent{kOtherPlaythrough});
        return false;
    }
    auto& host = m_world.GetHosting();
    if (!m_world.IsHosting())
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
    m_followers.Clear();
}
