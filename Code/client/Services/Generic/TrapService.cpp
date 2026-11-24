#include <Services/TrapService.h>

#include <Bridges/IActorBridge.h>
#include <Bridges/IFormBridge.h>
#include <CreatedIds.h>
#include <Events/TrapEvents.h>
#include <FormIds.h>
#include <Messages/Relayed.h>
#include <Services/ProjectileLaunches.h>
#include <Services/TransportService.h>
#include <SessionIds.h>
#include <World.h>
#include <Diagnostics/Counters.h>

TrapService::TrapService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport, IActorBridge& aActorBridge,
                         IFormBridge& aFormBridge) noexcept
    : m_world(aWorld), m_transport(aTransport), m_actorBridge(aActorBridge), m_formBridge(aFormBridge)
{
    Listen<TrapContactEvent, &TrapService::OnTrapContact>(m_connections, aDispatcher, this);
    Listen<TrapHitEvent, &TrapService::OnTrapHit>(m_connections, aDispatcher, this);
    Listen<NotifyTrapHit, &TrapService::OnTrapHitNotify>(m_connections, aDispatcher, this);
    Listen<NotifyWorldProjectile, &TrapService::OnWorldProjectile>(m_connections, aDispatcher, this);
}

std::optional<GameId> TrapService::ToWire(uint32_t aFormId) const noexcept
{
    return IsCreatedForm(aFormId) ? m_world.ctx().at<CreatedIds>().ToSession(aFormId) : ToSession(aFormId);
}

uint32_t TrapService::FromWire(const GameId& acId) const noexcept
{
    return IsCreatedId(acId) ? m_world.ctx().at<CreatedIds>().ToGame(acId) : ToGame(acId);
}

void TrapService::OnTrapContact(const TrapContactEvent& acEvent) const noexcept
{
    const auto cTrap = ToWire(acEvent.TrapId);
    const auto cTarget = Utils::GetSessionIdByFormId(acEvent.TargetId);
    if (!m_transport.IsOnline() || !cTrap || !cTarget)
        return;
    TrapContactRequest request;
    static_cast<Payloads::TrapContact&>(request) = acEvent.Contact;
    request.Trap = *cTrap;
    request.TargetId = *cTarget;
    m_transport.Send(request);
}

void TrapService::OnTrapHit(const TrapHitEvent& acEvent) const noexcept
{
    const auto cTrap = ToWire(acEvent.TrapId);
    const auto cTarget = Utils::GetSessionIdByFormId(acEvent.TargetId);
    if (!m_transport.IsOnline() || !cTarget)
        return;
    TrapHitRequest request;
    static_cast<Payloads::TrapHit&>(request) = acEvent.Hit;
    request.Trap = cTrap.value_or(GameId{});
    request.TargetId = *cTarget;
    spdlog::info("TrapService: {:X} hits {:X} for {:.1f}, sent to its game", acEvent.TrapId, acEvent.TargetId, acEvent.Hit.Damage);
    m_transport.Send(request);
}

void TrapService::OnTrapHitNotify(const NotifyTrapHit& acMessage) const noexcept
{
    const uint32_t cTarget = Utils::GetFormIdBySessionId(acMessage.TargetId);
    if (!cTarget || !m_actorBridge.ActorExists(cTarget) || m_actorBridge.IsActorCopy(cTarget))
        return;
    const uint32_t cTrap = FromWire(acMessage.Trap);
    spdlog::info("TrapService: {:X} takes the hit of {:X} for {:.1f}", cTarget, cTrap, acMessage.Damage);
    m_actorBridge.TakeTrapHit(cTarget, cTrap, acMessage);
}

void TrapService::OnWorldProjectile(const NotifyWorldProjectile& acMessage) const noexcept
{
    const uint32_t cShooter = FromWire(acMessage.Shooter);
    if (!cShooter || !m_formBridge.FormExists(cShooter))
        return;
    static auto& s_launched = Diagnostics::Counters::Get("world projectiles launched");
    Diagnostics::Counters::Add(s_launched, 1);
    ProjectileLaunches::Launch(m_formBridge, cShooter, acMessage.Launch);
}
