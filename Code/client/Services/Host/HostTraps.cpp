#include <Services/Host/HostTraps.h>

#include <CreatedIds.h>
#include <Diagnostics/Counters.h>
#include <Events/ProjectileLaunchedEvent.h>
#include <Events/TrapEvents.h>
#include <FormIds.h>
#include <Messages/Relayed.h>
#include <Services/ProjectileLaunches.h>
#include <Services/TransportService.h>
#include <SessionIds.h>
#include <World.h>

HostTraps::HostTraps(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
{
    Listen<TrapHitEvent, &HostTraps::OnTrapHit>(m_connections, aDispatcher, this);
    Listen<ProjectileLaunchedEvent, &HostTraps::OnProjectileLaunched>(m_connections, aDispatcher, this);
}

std::optional<GameId> HostTraps::ToWire(uint32_t aFormId) const noexcept
{
    return IsCreatedForm(aFormId) ? m_world.ctx().at<CreatedIds>().ToSession(aFormId) : ToSession(aFormId);
}

void HostTraps::OnTrapHit(const TrapHitEvent& acEvent) const noexcept
{
    const auto cTarget = Utils::GetSessionIdByFormId(acEvent.TargetId);
    if (!cTarget)
        return;
    TrapHitRequest request;
    static_cast<Payloads::TrapHit&>(request) = acEvent.Hit;
    request.Trap = ToWire(acEvent.TrapId).value_or(GameId{});
    request.TargetId = *cTarget;
    spdlog::info("HostTraps: {:X} hits {:X} for {:.1f}, sent to its game", acEvent.TrapId, acEvent.TargetId, acEvent.Hit.Damage);
    m_world.GetTransport().Send(request);
}

void HostTraps::OnProjectileLaunched(const ProjectileLaunchedEvent& acEvent) const noexcept
{
    // An object's, not an actor's; a spell's is shown by its cast.
    if (!acEvent.ByObject || acEvent.SpellID)
        return;
    const auto cShooter = ToWire(acEvent.ShooterID);
    if (!cShooter)
        return;
    WorldProjectileRequest request;
    request.Shooter = *cShooter;
    ProjectileLaunches::Fill(request.Launch, acEvent);
    static auto& s_sent = Diagnostics::Counters::Get("world projectiles sent");
    Diagnostics::Counters::Add(s_sent, 1);
    m_world.GetTransport().Send(request);
}
