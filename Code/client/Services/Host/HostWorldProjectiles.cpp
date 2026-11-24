#include <Services/Host/HostWorldProjectiles.h>

#include <CreatedIds.h>
#include <Diagnostics/Counters.h>
#include <Events/ProjectileLaunchedEvent.h>
#include <FormIds.h>
#include <Messages/Relayed.h>
#include <Services/ProjectileLaunches.h>
#include <Services/TransportService.h>
#include <SessionIds.h>
#include <World.h>

HostWorldProjectiles::HostWorldProjectiles(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
{
    Listen<ProjectileLaunchedEvent, &HostWorldProjectiles::OnProjectileLaunched>(m_connections, aDispatcher, this);
}

void HostWorldProjectiles::OnProjectileLaunched(const ProjectileLaunchedEvent& acEvent) const noexcept
{
    // An object's, not an actor's; a spell's is shown by its cast.
    if (!acEvent.ByObject || acEvent.SpellID)
        return;
    const auto cShooter = IsCreatedForm(acEvent.ShooterID) ? m_world.ctx().at<CreatedIds>().ToSession(acEvent.ShooterID) : ToSession(acEvent.ShooterID);
    if (!cShooter)
        return;
    WorldProjectileRequest request;
    request.Shooter = *cShooter;
    ProjectileLaunches::Fill(request.Launch, acEvent);
    static auto& s_sent = Diagnostics::Counters::Get("world projectiles sent");
    Diagnostics::Counters::Add(s_sent, 1);
    m_world.GetTransport().Send(request);
}
