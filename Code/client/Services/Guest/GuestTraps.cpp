#include <Services/Guest/GuestTraps.h>

#include <Bridges/IActorBridge.h>
#include <Bridges/IFormBridge.h>
#include <CreatedIds.h>
#include <Diagnostics/Counters.h>
#include <Events/TrapEvents.h>
#include <FormIds.h>
#include <Messages/Relayed.h>
#include <Services/ProjectileLaunches.h>
#include <Services/TransportService.h>
#include <SessionIds.h>
#include <World.h>

GuestTraps::GuestTraps(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
{
    Listen<TrapContactEvent, &GuestTraps::OnTrapContact>(m_connections, aDispatcher, this);
    Listen<NotifyTrapHit, &GuestTraps::OnTrapHit>(m_connections, aDispatcher, this);
    Listen<NotifyWorldProjectile, &GuestTraps::OnWorldProjectile>(m_connections, aDispatcher, this);
}

std::optional<GameId> GuestTraps::ToWire(uint32_t aFormId) const noexcept
{
    return IsCreatedForm(aFormId) ? m_world.ctx().at<CreatedIds>().ToSession(aFormId) : ToSession(aFormId);
}

uint32_t GuestTraps::FromWire(const GameId& acId) const noexcept
{
    return IsCreatedId(acId) ? m_world.ctx().at<CreatedIds>().ToGame(acId) : ToGame(acId);
}

void GuestTraps::OnTrapContact(const TrapContactEvent& acEvent) const noexcept
{
    const auto cTrap = ToWire(acEvent.TrapId);
    const auto cTarget = Utils::GetSessionIdByFormId(acEvent.TargetId);
    if (!m_world.GetTransport().IsOnline() || !cTrap || !cTarget)
        return;
    TrapContactRequest request;
    static_cast<Payloads::TrapContact&>(request) = acEvent.Contact;
    request.Trap = *cTrap;
    request.TargetId = *cTarget;
    m_world.GetTransport().Send(request);
}

void GuestTraps::OnTrapHit(const NotifyTrapHit& acMessage) const noexcept
{
    auto& actors = m_world.ctx().at<IActorBridge&>();
    const uint32_t cTarget = Utils::GetFormIdBySessionId(acMessage.TargetId);
    if (!cTarget || !actors.ActorExists(cTarget) || actors.IsActorCopy(cTarget))
        return;
    const uint32_t cTrap = FromWire(acMessage.Trap);
    spdlog::info("GuestTraps: {:X} takes the hit of {:X} for {:.1f}", cTarget, cTrap, acMessage.Damage);
    actors.TakeTrapHit(cTarget, cTrap, acMessage);
}

void GuestTraps::OnWorldProjectile(const NotifyWorldProjectile& acMessage) const noexcept
{
    auto& forms = m_world.ctx().at<IFormBridge&>();
    const uint32_t cShooter = FromWire(acMessage.Shooter);
    if (!cShooter || !forms.FormExists(cShooter))
        return;
    static auto& s_launched = Diagnostics::Counters::Get("world projectiles launched");
    Diagnostics::Counters::Add(s_launched, 1);
    ProjectileLaunches::Launch(forms, cShooter, acMessage.Launch);
}
