#include <Services/CombatService.h>
#include <Services/TransportService.h>

#include <Events/UpdateEvent.h>
#include <Games/Skyrim/Actor.h>
#include <Events/ProjectileLaunchedEvent.h>
#include <Events/HitEvent.h>

#include <Messages/ProjectileLaunchRequest.h>
#include <Messages/NotifyProjectileLaunch.h>

#include <Systems/ModSystem.h>
#include <World.h>

#include <Bridges/IActorBridge.h>
#include <Bridges/IFormBridge.h>
#include <Bridges/IPlayerBridge.h>
#include <Components.h>

CombatService::CombatService(World& aWorld, TransportService& aTransport, entt::dispatcher& aDispatcher, IFormBridge& aFormBridge,
                             IActorBridge& aActorBridge, IPlayerBridge& aPlayerBridge)
    : m_world(aWorld)
    , m_transport(aTransport)
    , m_formBridge(aFormBridge)
    , m_actorBridge(aActorBridge)
    , m_playerBridge(aPlayerBridge)
{
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&CombatService::OnUpdate>(this);
    m_hitConnection = aDispatcher.sink<HitEvent>().connect<&CombatService::OnHitEvent>(this);
    m_localComponentRemoved = m_world.on_destroy<LocalComponent>().connect<&CombatService::OnLocalComponentRemoved>(this);
    m_projectileLaunchedConnection = aDispatcher.sink<ProjectileLaunchedEvent>().connect<&CombatService::OnProjectileLaunchedEvent>(this);
    m_projectileLaunchConnection = aDispatcher.sink<NotifyProjectileLaunch>().connect<&CombatService::OnNotifyProjectileLaunch>(this);
}

void CombatService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    RunHitFeedback();
    UpdateRemotePlayerAnger();
    RefreshHangLockWatch();
    RunTargetUpdates(static_cast<float>(acEvent.Delta));
}

void CombatService::OnLocalComponentRemoved(entt::registry& aRegistry, entt::entity aEntity) const noexcept
{
    m_world.remove<CombatComponent>(aEntity);
}

void CombatService::OnProjectileLaunchedEvent(const ProjectileLaunchedEvent& acEvent) const noexcept
{
    ModSystem& modSystem = m_world.Get().GetModSystem();

    uint32_t shooterFormId = acEvent.ShooterID;
    auto view = m_world.view<FormIdComponent, LocalComponent>();
    const auto shooterEntityIt = std::find_if(std::begin(view), std::end(view), [shooterFormId, view](entt::entity entity) { return view.get<FormIdComponent>(entity).Id == shooterFormId; });

    if (shooterEntityIt == std::end(view))
    {
        spdlog::debug("Projectile launched by {:X}: not ours, not sent", shooterFormId);
        return;
    }

    LocalComponent& localComponent = view.get<LocalComponent>(*shooterEntityIt);

    ProjectileLaunchRequest request{};

    request.OriginX = acEvent.Origin.x;
    request.OriginY = acEvent.Origin.y;
    request.OriginZ = acEvent.Origin.z;

    modSystem.GetServerModId(acEvent.ProjectileBaseID, request.ProjectileBaseID);
    modSystem.GetServerModId(acEvent.WeaponID, request.WeaponID);
    modSystem.GetServerModId(acEvent.AmmoID, request.AmmoID);

    request.ShooterID = localComponent.Id;

    request.ZAngle = acEvent.ZAngle;
    request.XAngle = acEvent.XAngle;
    request.YAngle = acEvent.YAngle;

    modSystem.GetServerModId(acEvent.ParentCellID, request.ParentCellID);
    modSystem.GetServerModId(acEvent.SpellID, request.SpellID);

    request.CastingSource = acEvent.CastingSource;

    request.Area = acEvent.Area;
    request.Power = acEvent.Power;
    request.Scale = acEvent.Scale;

    request.AlwaysHit = acEvent.AlwaysHit;
    request.NoDamageOutsideCombat = acEvent.NoDamageOutsideCombat;
    request.AutoAim = acEvent.AutoAim;
    request.DeferInitialization = acEvent.DeferInitialization;
    request.ForceConeOfFire = acEvent.ForceConeOfFire;

    request.UnkBool1 = acEvent.UnkBool1;
    request.UnkBool2 = acEvent.UnkBool2;

    m_transport.Send(request);
    spdlog::debug("Projectile launched by {:X} (spell {:X}): sent", shooterFormId, acEvent.SpellID);
}

void CombatService::OnNotifyProjectileLaunch(const NotifyProjectileLaunch& acMessage) const noexcept
{
    ModSystem& modSystem = World::Get().GetModSystem();
    spdlog::debug("Projectile from server id {:X} received", acMessage.ShooterID);

    auto remoteView = m_world.view<RemoteComponent, FormIdComponent>();
    const auto remoteIt = std::find_if(std::begin(remoteView), std::end(remoteView), [remoteView, Id = acMessage.ShooterID](auto entity) { return remoteView.get<RemoteComponent>(entity).Id == Id; });

    if (remoteIt == std::end(remoteView))
    {
        spdlog::warn("Shooter with remote id {:X} not found.", acMessage.ShooterID);
        return;
    }

    FormIdComponent formIdComponent = remoteView.get<FormIdComponent>(*remoteIt);

    const uint32_t cParentCellId = modSystem.GetGameId(acMessage.ParentCellID);
    if (!m_formBridge.FormExists(cParentCellId))
    {
        spdlog::warn("Cannot launch projectile, invalid parent cell: {:X}", cParentCellId);
        return;
    }

    const uint32_t cProjectileBaseId = modSystem.GetGameId(acMessage.ProjectileBaseID);
    const uint32_t cWeaponId        = modSystem.GetGameId(acMessage.WeaponID);
    const uint32_t cAmmoId          = modSystem.GetGameId(acMessage.AmmoID);
    const uint32_t cSpellId         = modSystem.GetGameId(acMessage.SpellID);

    spdlog::debug("Projectile from server id {:X}: relaunching", acMessage.ShooterID);
    m_formBridge.LaunchProjectile(
        formIdComponent.Id,
        cParentCellId,
        acMessage.OriginX, acMessage.OriginY, acMessage.OriginZ,
        cProjectileBaseId, cWeaponId, cAmmoId,
        cSpellId, acMessage.CastingSource,
        acMessage.ZAngle, acMessage.XAngle, acMessage.YAngle,
        acMessage.Area, acMessage.Power, acMessage.Scale,
        acMessage.AlwaysHit, acMessage.NoDamageOutsideCombat, acMessage.AutoAim,
        false /* DeferInitialization */, acMessage.ForceConeOfFire,
        acMessage.UnkBool1, acMessage.UnkBool2);
}

void CombatService::OnHitEvent(const HitEvent& acEvent) noexcept
{
    // Hit feedback belongs to the attacker's game: our player's hit on another game's actor shows
    // its health bar now. The owner applies the hit (our copy never does), so the HUD may refuse
    // the copy while its health is still full; RunHitFeedback shows it again when the owner's
    // lower health arrives.
    if (acEvent.HitterId != m_playerBridge.GetPlayerFormId())
        return;
    auto view = m_world.view<FormIdComponent, RemoteComponent>();
    const auto cCopy = std::find_if(view.begin(), view.end(), [&](entt::entity aEntity) {
        return view.get<FormIdComponent>(aEntity).Id == acEvent.HitteeId;
    });
    if (cCopy == view.end())
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
        auto& hit = it.value();
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

void CombatService::RunTargetUpdates(const float acDelta) const noexcept
{
#if 0
    static std::chrono::steady_clock::time_point lastSendTimePoint;
    constexpr auto cDelayBetweenUpdates = 200ms;

    const auto now = std::chrono::steady_clock::now();
    if (now - lastSendTimePoint < cDelayBetweenUpdates)
        return;

    lastSendTimePoint = now;

    const auto view = m_world.view<FormIdComponent, CombatComponent>();

    Vector<entt::entity> toRemove{};

    for (const auto entity : view)
    {
        auto& combatComponent = view.get<CombatComponent>(entity);
        combatComponent.Timer = combatComponent.Timer - acDelta;

        if (combatComponent.Timer <= 0.f)
        {
            toRemove.push_back(entity);
            continue;
        }

        auto* pTarget = Cast<Actor>(TESForm::GetById(combatComponent.TargetFormId));
        if (!pTarget)
        {
            spdlog::warn("{}: : combat target not found, form id {:X}", __FUNCTION__, combatComponent.TargetFormId);
            toRemove.push_back(entity);
            continue;
        }

        const auto& formIdComponent = view.get<FormIdComponent>(entity);
        auto* pActor = Cast<Actor>(TESForm::GetById(formIdComponent.Id));
        if (!pActor)
        {
            spdlog::error("{}: : actor not found, form id {:X}", __FUNCTION__, formIdComponent.Id);
            toRemove.push_back(entity);
            continue;
        }
    }

    for (const auto entity : toRemove)
        m_world.remove<CombatComponent>(entity);
#endif
}
