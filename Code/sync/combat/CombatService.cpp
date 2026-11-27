#include <combat/CombatService.h>
#include <magic/ProjectileLaunches.h>
#include <core/TransportService.h>
#include <travel/TravelService.h>

#include <core/UpdateEvent.h>
#include <combat/ActorUpkeep.h>
#include <magic/ProjectileLaunchedEvent.h>
#include <combat/HitEvent.h>
#include <combat/MeleeStaggerEvent.h>
#include <core/SessionIds.h>

#include <Messages/Relayed.h>

#include <core/FormIds.h>
#include <core/World.h>

#include <ports/IActorBridge.h>
#include <ports/IFormBridge.h>
#include <ports/IPlayerBridge.h>
#include <core/FormIdComponent.h>
#include <core/LocalComponent.h>
#include <core/RemoteComponent.h>
#include <combat/CombatComponent.h>
#include <core/Profile.h>

CombatService::CombatService(World& aWorld, TransportService& aTransport, entt::dispatcher& aDispatcher, IFormBridge& aFormBridge,
                             IActorBridge& aActorBridge, IPlayerBridge& aPlayerBridge)
    : m_world(aWorld)
    , m_transport(aTransport)
    , m_formBridge(aFormBridge)
    , m_actorBridge(aActorBridge)
    , m_playerBridge(aPlayerBridge)
{
    Listen<UpdateEvent, &CombatService::OnUpdate>(m_connections, aDispatcher, this);
    Listen<HitEvent, &CombatService::OnHitEvent>(m_connections, aDispatcher, this);
    Listen<MeleeStaggerEvent, &CombatService::OnMeleeStagger>(m_connections, aDispatcher, this);
    Listen<NotifyMeleeStagger, &CombatService::OnNotifyMeleeStagger>(m_connections, aDispatcher, this);
    Listen<&CombatService::OnLocalComponentRemoved>(m_connections, m_world.on_destroy<LocalComponent>(), this);
    Listen<ProjectileLaunchedEvent, &CombatService::OnProjectileLaunchedEvent>(m_connections, aDispatcher, this);
    Listen<NotifyProjectileLaunch, &CombatService::OnNotifyProjectileLaunch>(m_connections, aDispatcher, this);
}

void CombatService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    PROFILE_ZONE("CombatService::OnUpdate");
    RunHitFeedback();
    UpdateRemotePlayerAnger();
    RefreshHangLockWatch();
}

void CombatService::OnLocalComponentRemoved(entt::registry& aRegistry, entt::entity aEntity) const noexcept
{
    m_world.remove<CombatComponent>(aEntity);
}

void CombatService::OnProjectileLaunchedEvent(const ProjectileLaunchedEvent& acEvent) const noexcept
{
    // What an object shoots (a dart port, a catapult, an activator's or a statue's spell) goes out with no shooter, of
    // any kind: the others show it with no shooter, which applies nothing there; its hits are the host's (projectiles.md).
    uint32_t shooterId = 0;
    if (acEvent.FromObject)
    {
        if (!m_transport.IsOnline())
            return;
    }
    else
    {
        const uint32_t cShooterFormId = acEvent.ShooterID;
        const auto cShooter = m_world.GetActorIndex().ByForm<LocalComponent>(cShooterFormId);
        if (cShooter == entt::null)
        {
            spdlog::debug("Projectile launched by {:X}: not ours, not sent", cShooterFormId);
            return;
        }
        shooterId = m_world.get<LocalComponent>(cShooter).Id;
    }

    ProjectileLaunchRequest request{};

    request.ShooterID = shooterId;
    ProjectileLaunches::Fill(request, acEvent);

    m_transport.Send(request);
    spdlog::debug("Projectile launched by {:X} (spell {:X}): sent", acEvent.ShooterID, acEvent.SpellID);
}

void CombatService::OnNotifyProjectileLaunch(const NotifyProjectileLaunch& acMessage) const noexcept
{
    spdlog::debug("Projectile from session id {:X} received", acMessage.ShooterID);

    // Shooter 0: what an object shot, relaunched with no shooter (OnProjectileLaunchedEvent).
    uint32_t shooterFormId = 0;
    if (acMessage.ShooterID)
    {
        const auto cShooter = m_world.GetActorIndex().BySession<RemoteComponent, FormIdComponent>(acMessage.ShooterID);
        if (cShooter == entt::null)
        {
            spdlog::warn("Shooter with remote id {:X} not found.", acMessage.ShooterID);
            return;
        }
        shooterFormId = m_world.get<FormIdComponent>(cShooter).Id;
    }

    const uint32_t cParentCellId = m_world.GetTravelService().CellAt(
        acMessage.WorldSpaceID, acMessage.ParentCellID, {acMessage.OriginX, acMessage.OriginY, acMessage.OriginZ});
    spdlog::debug("Projectile from session id {:X}: relaunching", acMessage.ShooterID);
    ProjectileLaunches::Launch(m_formBridge, shooterFormId, cParentCellId, acMessage);
}

void CombatService::OnMeleeStagger(const MeleeStaggerEvent& acEvent) noexcept
{
    if (!m_transport.IsOnline() || acEvent.HitterId != m_playerBridge.GetPlayerFormId())
        return;
    const auto cHittee = Utils::GetSessionIdByFormId(acEvent.HitteeId);
    const auto cHitter = Utils::GetSessionIdByFormId(acEvent.HitterId);
    if (!cHittee || !cHitter || !m_actorBridge.IsActorCopy(acEvent.HitteeId))
        return;
    MeleeStaggerRequest request;
    request.HitteeId = *cHittee;
    request.HitterId = *cHitter;
    request.Magnitude = acEvent.Magnitude;
    m_transport.Send(request);
}

void CombatService::OnNotifyMeleeStagger(const NotifyMeleeStagger& acMessage) noexcept
{
    const uint32_t cHittee = Utils::GetFormIdBySessionId(acMessage.HitteeId);
    if (cHittee && m_actorBridge.ActorExists(cHittee) && !m_actorBridge.IsActorCopy(cHittee))
        m_actorBridge.StaggerActor(cHittee, acMessage.Magnitude, Utils::GetFormIdBySessionId(acMessage.HitterId));
}

void CombatService::OnHitEvent(const HitEvent& acEvent) noexcept
{
    // Hit feedback belongs to the attacker's game: our player's hit on another game's actor shows
    // its health bar now. The owner applies the hit (our copy never does), so the HUD may refuse
    // the copy while its health is still full; RunHitFeedback shows it again when the owner's
    // lower health arrives.
    if (acEvent.HitterId != m_playerBridge.GetPlayerFormId())
        return;
    if (m_world.GetActorIndex().ByForm<RemoteComponent>(acEvent.HitteeId) == entt::null)
        return;

    const float cHealth = m_actorBridge.GetActorValue(acEvent.HitteeId, 24 /* kHealth */);
    if (!m_recentHits.contains(acEvent.HitteeId))
        m_recentHits[acEvent.HitteeId].Health = cHealth;
    m_recentHits[acEvent.HitteeId].At = std::chrono::steady_clock::now();
    m_playerBridge.ShowEnemyHealth(acEvent.HitteeId);
}

void CombatService::RunHitFeedback() noexcept
{
    constexpr auto cWatchFor = std::chrono::seconds(5);
    const auto cNow = std::chrono::steady_clock::now();
    for (auto it = m_recentHits.begin(); it != m_recentHits.end();)
    {
        auto& hit = it->second;
        if (cNow - hit.At > cWatchFor)
        {
            it = m_recentHits.erase(it);
            continue;
        }
        const float cHealth = m_actorBridge.GetActorValue(it->first, 24 /* kHealth */);
        if (cHealth < hit.Health)
            m_playerBridge.ShowEnemyHealth(it->first);
        hit.Health = cHealth;
        ++it;
    }
}
