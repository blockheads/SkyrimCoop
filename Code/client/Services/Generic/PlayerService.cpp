#include <Services/PlayerService.h>

#include <World.h>

#include <Events/UpdateEvent.h>
#include <Events/ConnectedEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/BeastFormChangeEvent.h>

#include <Messages/PlayerRespawnRequest.h>
#include <Messages/NotifyPlayerRespawn.h>
#include <Messages/PlayerLevelRequest.h>

#include <Structs/ServerSettings.h>

#include <Bridges/IPlayerBridge.h>
#include <Bridges/IFormBridge.h>
#include <Bridges/IActorBridge.h>

#include <spdlog/spdlog.h>
#include <spdlog/fmt/fmt.h>

// ActorValueInfo::kHealth = 24 (Skyrim SE enum value)
static constexpr int32_t kHealthActorValue = 24;
// ActorValueOwner::ForceMode::DAMAGE = 2
static constexpr int32_t kForceModeDamage = 2;
// Skyrim race form IDs for beast form detection
static constexpr uint32_t kWerewolfRaceFormId   = 0x200283A;
static constexpr uint32_t kVampireLordRaceFormId = 0xCDD84;
// Skyrim TESGlobal form IDs used to control game features
static constexpr uint32_t kKillMoveGlobalFormId          = 0x100F19;
static constexpr uint32_t kWorldEncountersGlobalFormId   = 0xB8EC1;

PlayerService::PlayerService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport,
                             IPlayerBridge& aPlayerBridge, IFormBridge& aFormBridge, IActorBridge& aActorBridge) noexcept
    : m_world(aWorld)
    , m_dispatcher(aDispatcher)
    , m_transport(aTransport)
    , m_playerBridge(aPlayerBridge)
    , m_formBridge(aFormBridge)
    , m_actorBridge(aActorBridge)
{
    m_updateConnection = m_dispatcher.sink<UpdateEvent>().connect<&PlayerService::OnUpdate>(this);
    m_connectedConnection = m_dispatcher.sink<ConnectedEvent>().connect<&PlayerService::OnConnected>(this);
    m_disconnectedConnection = m_dispatcher.sink<DisconnectedEvent>().connect<&PlayerService::OnDisconnected>(this);
    m_settingsConnection = m_dispatcher.sink<ServerSettings>().connect<&PlayerService::OnServerSettingsReceived>(this);
    m_notifyRespawnConnection = m_dispatcher.sink<NotifyPlayerRespawn>().connect<&PlayerService::OnNotifyPlayerRespawn>(this);
}

void PlayerService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    RunRespawnUpdates(acEvent.Delta);
    RunPostDeathUpdates(acEvent.Delta);
    RunDifficultyUpdates();
    RunLevelUpdates();
    RunBeastFormDetection();
}

void PlayerService::OnConnected(const ConnectedEvent& acEvent) noexcept
{
    // TODO: SkyrimTogether.esm
    m_formBridge.SetGlobalValue(kKillMoveGlobalFormId, 0.f);
    // World encounters are the host's world: only its game spawns them.
    m_formBridge.SetGlobalValue(kWorldEncountersGlobalFormId, m_world.GetHostService().IsHosting() ? 1.f : 0.f);
}

void PlayerService::OnDisconnected(const DisconnectedEvent& acEvent) noexcept
{
    // A friend's game gets its own difficulty back; the host's was never changed.
    if (m_previousDifficulty)
        m_playerBridge.SetDifficulty(static_cast<uint32_t>(*m_previousDifficulty));
    m_previousDifficulty.reset();
    m_serverDifficulty.reset();

    ToggleDeathSystem(false);

    m_formBridge.SetGlobalValue(kKillMoveGlobalFormId, 1.f);

    // Restore to the default value (150 in skyrim, 175 in fallout 4)
    m_playerBridge.SetGreetDistance(150.f);

    m_formBridge.SetGlobalValue(kWorldEncountersGlobalFormId, 1.f);
}

void PlayerService::OnServerSettingsReceived(const ServerSettings& acSettings) noexcept
{
    // The session's difficulty is the host's: a friend's game takes it (and keeps its own for
    // leaving, from the first settings of the session); the host's game is where it comes from.
    if (!m_world.GetHostService().IsHosting())
    {
        if (!m_previousDifficulty)
            m_previousDifficulty = m_playerBridge.GetDifficulty();
        m_serverDifficulty = acSettings.Difficulty;
        m_playerBridge.SetDifficulty(acSettings.Difficulty);
    }

    if (!acSettings.GreetingsEnabled)
    {
        m_playerBridge.SetGreetDistance(0.f);
    }

    ToggleDeathSystem(acSettings.DeathSystemEnabled);
}

void PlayerService::OnNotifyPlayerRespawn(const NotifyPlayerRespawn& acMessage) const noexcept
{
    m_playerBridge.PlayerPayGold(acMessage.GoldLost);

    std::string message = fmt::format("You died and lost {} gold.", acMessage.GoldLost);
    m_playerBridge.ShowHudMessage(message);
}


void PlayerService::RunRespawnUpdates(const double acDeltaTime) noexcept
{
    if (!m_isDeathSystemEnabled)
        return;

    static bool s_startTimer = false;

    const uint32_t cPlayerId = m_playerBridge.GetPlayerFormId();

    if (!m_playerBridge.IsPlayerBleedingOut())
    {
        m_cachedMainSpellId = m_playerBridge.GetPlayerActiveMagicItemFormId(0);
        m_cachedSecondarySpellId = m_playerBridge.GetPlayerActiveMagicItemFormId(1);
        m_cachedPowerId = m_playerBridge.GetPlayerEquippedShoutFormId();

        s_startTimer = false;
        return;
    }

    if (!s_startTimer)
    {
        s_startTimer = true;
        m_respawnTimer = 5.0;
        m_playerBridge.FadeScreen(true, true, 3.0f, true, 2.0f);

        // If a player dies not by its health reaching 0, getting it up from its bleedout state isn't possible
        // just by setting its health back to max. Therefore, put it to 0.
        if (m_playerBridge.GetPlayerActorValue(kHealthActorValue) > 0.f)
            m_playerBridge.ForcePlayerActorValue(kForceModeDamage, kHealthActorValue, 0.f);

        m_playerBridge.PlayerPayCrimeGoldToAllFactions();
    }

    m_respawnTimer -= acDeltaTime;

    if (m_respawnTimer <= 0.0)
    {
        m_playerBridge.RespawnPlayer();

        m_knockdownTimer = 1.5;
        m_knockdownStart = true;

        m_transport.Send(PlayerRespawnRequest());

        s_startTimer = false;

        if (m_cachedMainSpellId)
            m_actorBridge.ActorEquipSpell(cPlayerId, m_cachedMainSpellId, 0);
        if (m_cachedSecondarySpellId)
            m_actorBridge.ActorEquipSpell(cPlayerId, m_cachedSecondarySpellId, 1);
        if (m_cachedPowerId)
            m_actorBridge.ActorEquipShout(cPlayerId, m_cachedPowerId);
    }
}

// Doesn't seem to respawn quite yet
void PlayerService::RunPostDeathUpdates(const double acDeltaTime) noexcept
{
    if (!m_isDeathSystemEnabled)
        return;

    // If a player dies in ragdoll, it gets stuck.
    // This code ragdolls the player again upon respawning.
    // It also makes the player invincible for 5 seconds.
    if (m_knockdownStart)
    {
        m_knockdownTimer -= acDeltaTime;
        if (m_knockdownTimer <= 0.0)
        {
            m_playerBridge.SetPlayerGodMode(true);
            m_godmodeStart = true;
            m_godmodeTimer = 10.0;

            float x = 0.f, y = 0.f, z = 0.f;
            m_playerBridge.GetPlayerPosition(x, y, z);
            m_actorBridge.PlayerKnockExplosion(m_playerBridge.GetPlayerFormId(), x, y, z, 0.f);

            m_playerBridge.FadeScreen(false, true, 0.5f, true, 2.f);

            m_knockdownStart = false;
        }
    }

    if (m_godmodeStart)
    {
        m_godmodeTimer -= acDeltaTime;
        if (m_godmodeTimer <= 0.0)
        {
            m_playerBridge.SetPlayerGodMode(false);

            m_godmodeStart = false;
        }
    }
}

void PlayerService::RunDifficultyUpdates() const noexcept
{
    // A friend's game holds the host's difficulty (its settings menu cannot change it away).
    if (!m_transport.IsConnected() || !m_serverDifficulty || m_playerBridge.GetDifficulty() == *m_serverDifficulty)
        return;

    m_playerBridge.SetDifficulty(*m_serverDifficulty);
}

void PlayerService::RunLevelUpdates() const noexcept
{
    // The LevelUp hook is kinda weird, so ehh, just check periodically, doesn't really cost anything.

    static std::chrono::steady_clock::time_point lastSendTimePoint;
    constexpr auto cDelayBetweenUpdates = 1000ms;

    const auto now = std::chrono::steady_clock::now();
    if (now - lastSendTimePoint < cDelayBetweenUpdates)
        return;

    lastSendTimePoint = now;

    static uint16_t oldLevel = m_playerBridge.GetPlayerLevel();

    uint16_t newLevel = m_playerBridge.GetPlayerLevel();
    if (newLevel != oldLevel)
    {
        PlayerLevelRequest request{};
        request.NewLevel = newLevel;

        m_transport.Send(request);

        oldLevel = newLevel;
    }
}

void PlayerService::RunBeastFormDetection() const noexcept
{
    static uint32_t lastRaceFormId = 0;
    static std::chrono::steady_clock::time_point lastSendTimePoint;
    constexpr auto cDelayBetweenUpdates = 250ms;

    const auto now = std::chrono::steady_clock::now();
    if (now - lastSendTimePoint < cDelayBetweenUpdates)
        return;

    lastSendTimePoint = now;

    const uint32_t raceFormId = m_playerBridge.GetPlayerRaceFormId();
    if (!raceFormId)
        return;

    if (raceFormId == lastRaceFormId)
        return;

    if (raceFormId == kWerewolfRaceFormId || raceFormId == kVampireLordRaceFormId)
        m_world.GetDispatcher().trigger(BeastFormChangeEvent());

    lastRaceFormId = raceFormId;
}

void PlayerService::ToggleDeathSystem(bool aSet) noexcept
{
    m_isDeathSystemEnabled = aSet;

    m_playerBridge.SetPlayerRespawnMode(aSet);
}
