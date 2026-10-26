#include <Services/PlayerService.h>

#include <World.h>

#include <Events/ConnectedEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/MenuEvent.h>
#include <Events/PlayerLooksChangeEvent.h>
#include <Events/PlayerFormChangeEvent.h>
#include <Events/DifficultyChangeEvent.h>

#include <Messages/PlayerLevelRequest.h>
#include <Messages/NotifySettingsChange.h>

#include <Bridges/IPlayerBridge.h>
#include <Bridges/IFormBridge.h>

#include <spdlog/spdlog.h>
#include <Services/HostService.h>

// The race menu (RaceSexMenu): what it changes reaches the copies once it closes.
static constexpr const char* kRaceMenu = "RaceSex Menu";
// Skyrim TESGlobal form IDs used to control game features
static constexpr uint32_t kWorldEncountersGlobalFormId   = 0xB8EC1;

PlayerService::PlayerService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport, IPlayerBridge& aPlayerBridge,
                             IFormBridge& aFormBridge) noexcept
    : m_world(aWorld)
    , m_dispatcher(aDispatcher)
    , m_transport(aTransport)
    , m_playerBridge(aPlayerBridge)
    , m_formBridge(aFormBridge)
{
    Listen<ConnectedEvent, &PlayerService::OnConnected>(m_connections, m_dispatcher, this);
    Listen<DisconnectedEvent, &PlayerService::OnDisconnected>(m_connections, m_dispatcher, this);
    Listen<NotifySettingsChange, &PlayerService::OnDifficultyChange>(m_connections, m_dispatcher, this);
    Listen<DifficultyChangeEvent, &PlayerService::OnDifficultySet>(m_connections, m_dispatcher, this);
    Listen<PlayerFormChangeEvent, &PlayerService::OnPlayerFormChange>(m_connections, m_dispatcher, this);
    Listen<MenuEvent, &PlayerService::OnMenu>(m_connections, m_dispatcher, this);
}

void PlayerService::OnConnected(const ConnectedEvent&) noexcept
{
    // What changes from here is sent on PlayerFormChangeEvent.
    m_lastLevel = m_playerBridge.GetPlayerLevel();
    m_lastRaceFormId = m_playerBridge.GetPlayerRaceFormId();
    // World encounters are the host's world: only its game spawns them.
    m_formBridge.SetGlobalValue(kWorldEncountersGlobalFormId, m_world.GetHostService().IsHosting() ? 1.f : 0.f);
}

void PlayerService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    // A guest gets its own difficulty back; the host's was never changed.
    if (m_previousDifficulty)
        m_playerBridge.SetDifficulty(static_cast<uint32_t>(*m_previousDifficulty));
    m_previousDifficulty.reset();
    m_hostDifficulty.reset();

    // Restore Skyrim's default greet distance.
    m_playerBridge.SetGreetDistance(150.f);

    m_formBridge.SetGlobalValue(kWorldEncountersGlobalFormId, 1.f);
}

void PlayerService::OnDifficultyChange(const NotifySettingsChange& acMessage) noexcept
{
    // A guest holds the host's difficulty and keeps its own for leaving; the host's game supplies it.
    if (!m_world.GetHostService().IsHosting())
    {
        if (!m_previousDifficulty)
            m_previousDifficulty = m_playerBridge.GetDifficulty();
        m_hostDifficulty = acMessage.Difficulty;
        m_playerBridge.SetDifficulty(acMessage.Difficulty);
    }

    m_playerBridge.SetGreetDistance(0.f);
}

void PlayerService::OnDifficultySet(const DifficultyChangeEvent& acEvent) const noexcept
{
    if (m_transport.IsConnected() && m_hostDifficulty && acEvent.Difficulty != *m_hostDifficulty)
        m_playerBridge.SetDifficulty(*m_hostDifficulty);
}

void PlayerService::OnPlayerFormChange(const PlayerFormChangeEvent&) noexcept
{
    const uint16_t cLevel = m_playerBridge.GetPlayerLevel();
    if (cLevel != m_lastLevel)
    {
        PlayerLevelRequest request{};
        request.NewLevel = cLevel;
        m_transport.Send(request);
        m_lastLevel = cLevel;
    }

    const uint32_t cRaceFormId = m_playerBridge.GetPlayerRaceFormId();
    if (!cRaceFormId || cRaceFormId == m_lastRaceFormId)
        return;
    m_lastRaceFormId = cRaceFormId;
    if (!m_inRaceMenu)
        m_world.GetDispatcher().trigger(PlayerLooksChangeEvent());
}

void PlayerService::OnMenu(const MenuEvent& acEvent) noexcept
{
    if (acEvent.Menu != kRaceMenu)
        return;
    m_inRaceMenu = acEvent.Opening;
    if (acEvent.Opening || !m_transport.IsConnected())
        return;
    m_lastRaceFormId = m_playerBridge.GetPlayerRaceFormId();
    m_world.GetDispatcher().trigger(PlayerLooksChangeEvent());
}
