#include <Services/PlayerService.h>

#include <World.h>

#include <Events/UpdateEvent.h>
#include <Events/ConnectedEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/BeastFormChangeEvent.h>
#include <Events/PlayerFormChangeEvent.h>
#include <Events/DifficultyChangeEvent.h>

#include <Messages/PlayerRespawnRequest.h>
#include <Messages/PlayerLevelRequest.h>

#include <Messages/NotifySettingsChange.h>

#include <Bridges/IPlayerBridge.h>
#include <Bridges/IFormBridge.h>
#include <Bridges/IActorBridge.h>

#include <spdlog/spdlog.h>
#include <Services/HostService.h>
#include <Services/TravelService.h>

// ActorValueInfo::kHealth = 24 (Skyrim SE enum value)
static constexpr int32_t kHealthActorValue = 24;
// ActorValueOwner::ForceMode::DAMAGE = 2
static constexpr int32_t kForceModeDamage = 2;
// Skyrim race form IDs for beast form detection
static constexpr uint32_t kWerewolfRaceFormId   = 0x200283A;
static constexpr uint32_t kVampireLordRaceFormId = 0xCDD84;
// Skyrim TESGlobal form IDs used to control game features
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
    Listen<UpdateEvent, &PlayerService::OnUpdate>(m_connections, m_dispatcher, this);
    Listen<ConnectedEvent, &PlayerService::OnConnected>(m_connections, m_dispatcher, this);
    Listen<DisconnectedEvent, &PlayerService::OnDisconnected>(m_connections, m_dispatcher, this);
    Listen<NotifySettingsChange, &PlayerService::OnDifficultyChange>(m_connections, m_dispatcher, this);
    Listen<DifficultyChangeEvent, &PlayerService::OnDifficultySet>(m_connections, m_dispatcher, this);
    Listen<PlayerFormChangeEvent, &PlayerService::OnPlayerFormChange>(m_connections, m_dispatcher, this);
}

void PlayerService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    RunRespawnUpdates(acEvent.Delta);
    RunPostDeathUpdates(acEvent.Delta);
}

void PlayerService::OnConnected(const ConnectedEvent& acEvent) noexcept
{
    // What changes from here is sent on PlayerFormChangeEvent.
    m_lastLevel = m_playerBridge.GetPlayerLevel();
    m_lastRaceFormId = m_playerBridge.GetPlayerRaceFormId();
    // World encounters are the host's world: only its game spawns them.
    m_formBridge.SetGlobalValue(kWorldEncountersGlobalFormId, m_world.GetHostService().IsHosting() ? 1.f : 0.f);
}

void PlayerService::OnDisconnected(const DisconnectedEvent& acEvent) noexcept
{
    // A guest gets its own difficulty back; the host's was never changed.
    if (m_previousDifficulty)
        m_playerBridge.SetDifficulty(static_cast<uint32_t>(*m_previousDifficulty));
    m_previousDifficulty.reset();
    m_hostDifficulty.reset();

    ToggleDeathSystem(false);

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
    ToggleDeathSystem(true);
}


void PlayerService::RunRespawnUpdates(const double acDeltaTime) noexcept
{
    if (!m_isDeathSystemEnabled)
        return;

    const uint32_t cPlayerId = m_playerBridge.GetPlayerFormId();

    if (!m_playerBridge.IsPlayerBleedingOut())
    {
        m_cachedMainSpellId = m_playerBridge.GetPlayerActiveMagicItemFormId(0);
        m_cachedSecondarySpellId = m_playerBridge.GetPlayerActiveMagicItemFormId(1);
        m_cachedPowerId = m_playerBridge.GetPlayerEquippedShoutFormId();

        m_respawnStarted = false;
        return;
    }

    if (!m_respawnStarted)
    {
        m_respawnStarted = true;
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
        // It moves the player (to its cell's COC marker): a travel that takes nobody along.
        m_world.GetTravelService().OnRespawnMove();
        m_playerBridge.RespawnPlayer();

        m_knockdownTimer = 1.5;
        m_knockdownStart = true;

        m_transport.Send(PlayerRespawnRequest());

        m_respawnStarted = false;

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
    if (cRaceFormId == kWerewolfRaceFormId || cRaceFormId == kVampireLordRaceFormId)
        m_world.GetDispatcher().trigger(BeastFormChangeEvent());
    m_lastRaceFormId = cRaceFormId;
}

void PlayerService::ToggleDeathSystem(bool aSet) noexcept
{
    m_isDeathSystemEnabled = aSet;

    m_playerBridge.SetPlayerRespawnMode(aSet);
}
