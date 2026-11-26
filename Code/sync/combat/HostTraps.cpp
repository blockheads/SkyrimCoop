#include <combat/HostTraps.h>

#include <core/CreatedIds.h>
#include <combat/TrapEvents.h>
#include <core/FormIds.h>
#include <Messages/Relayed.h>
#include <core/TransportService.h>
#include <core/SessionIds.h>
#include <core/World.h>

HostTraps::HostTraps(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
{
    Listen<TrapHitEvent, &HostTraps::OnTrapHit>(m_connections, aDispatcher, this);
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
