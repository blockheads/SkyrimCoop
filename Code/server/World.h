#pragma once

#include "Services/AdminService.h"

#include <Services/PlayerService.h>
#include <Services/PartyService.h>
#include <Services/CharacterService.h>
#include <Services/CalendarService.h>
#include <Services/QuestService.h>
#include <Services/ScriptService.h>
#include <Services/RunnerService.h>

#include "Game/PlayerManager.h"
#include <Systems/ModSystem.h>

class NetworkBridge;

namespace ESLoader
{
struct RecordCollection;
}

namespace Server
{

struct World : entt::registry
{
    World();
    ~World() noexcept;

    TP_NOCOPYMOVE(World);

    // P2P: Update loop (runs game + handles peers)
    void Update() noexcept;

    // P2P: Hosting functionality
    bool StartHosting(uint16_t aPort = 10578, uint8_t aMaxPeers = 8) noexcept;
    void StopHosting() noexcept;
    [[nodiscard]] bool IsHosting() const noexcept;

    // P2P: Network bridge access
    NetworkBridge* GetNetworkBridge() noexcept { return m_pNetworkBridge.get(); }
    const NetworkBridge* GetNetworkBridge() const noexcept { return m_pNetworkBridge.get(); }

    entt::dispatcher& GetDispatcher() noexcept { return m_dispatcher; }
    const entt::dispatcher& GetDispatcher() const noexcept { return m_dispatcher; }
    CharacterService& GetCharacterService() noexcept { return ctx().at<CharacterService>(); }
    const CharacterService& GetCharacterService() const noexcept { return ctx().at<const CharacterService>(); }
    PlayerService& GetPlayerService() noexcept { return ctx().at<PlayerService>(); }
    const PlayerService& GetPlayerService() const noexcept { return ctx().at<const PlayerService>(); }
    PartyService& GetPartyService() noexcept { return ctx().at<PartyService>(); }
    const PartyService& GetPartyService() const noexcept { return ctx().at<const PartyService>(); }
    CalendarService& GetCalendarService() noexcept { return ctx().at<CalendarService>(); }
    const CalendarService& GetCalendarService() const noexcept { return ctx().at<const CalendarService>(); }
    QuestService& GetQuestService() noexcept { return ctx().at<QuestService>(); }
    const QuestService& GetQuestService() const noexcept { return ctx().at<const QuestService>(); }
    PlayerManager& GetPlayerManager() noexcept { return m_playerManager; }
    const PlayerManager& GetPlayerManager() const noexcept { return m_playerManager; }
    ScriptService& GetScriptService() const noexcept { return *m_pScriptService; }

    // P2P: Client services needed for host to run Skyrim
    RunnerService& GetRunner() noexcept { return m_runner; }
    ModSystem& GetModSystem() noexcept { return m_modSystem; }
    ImguiService& GetImguiService() noexcept { return ctx().at<ImguiService>(); }
    const ImguiService& GetImguiService() const noexcept { return ctx().at<const ImguiService>(); }
    OverlayService& GetOverlayService() noexcept { return ctx().at<OverlayService>(); }
    const OverlayService& GetOverlayService() const noexcept { return ctx().at<const OverlayService>(); }
    InputService& GetInputService() noexcept { return ctx().at<InputService>(); }
    const InputService& GetInputService() const noexcept { return ctx().at<const InputService>(); }
    DebugService& GetDebugService() noexcept { return ctx().at<DebugService>(); }
    const DebugService& GetDebugService() const noexcept { return ctx().at<const DebugService>(); }
    PapyrusService& GetPapyrusService() noexcept { return ctx().at<PapyrusService>(); }
    const PapyrusService& GetPapyrusService() const noexcept { return ctx().at<const PapyrusService>(); }
    DiscordService& GetDiscordService() noexcept { return ctx().at<DiscordService>(); }
    const DiscordService& GetDiscordService() const noexcept { return ctx().at<const DiscordService>(); }

    // Null checked at start when MoPo is on!
    ESLoader::RecordCollection* GetRecordCollection() noexcept { return m_recordCollection.get(); }

    const ESLoader::RecordCollection* GetRecordCollection() const noexcept { return m_recordCollection.get(); }

    [[nodiscard]] static uint32_t ToInteger(entt::entity aEntity) { return to_integral(aEntity); }

private:
    entt::dispatcher m_dispatcher;

    TiltedPhoques::SharedPtr<AdminService> m_spAdminService;
    TiltedPhoques::UniquePtr<ScriptService> m_pScriptService;
    PlayerManager m_playerManager;
    UniquePtr<ESLoader::RecordCollection> m_recordCollection;

    // P2P: Client services needed for host to run Skyrim
    RunnerService m_runner;
    ModSystem m_modSystem;
    std::chrono::high_resolution_clock::time_point m_lastFrameTime;

    // P2P: Network bridge for peer-to-peer hosting (replaces GameServer)
    std::unique_ptr<NetworkBridge> m_pNetworkBridge;
};

} // namespace Server
