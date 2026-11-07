#include <Services/Guest/GuestCopies.h>

#include <Bridges/IActorBridge.h>
#include <Components.h>
#include <Events/CopyHeldEvent.h>
#include <World.h>

GuestCopies::GuestCopies(World& aWorld, entt::dispatcher& aDispatcher, IActorBridge& aActors) noexcept
    : m_world(aWorld)
    , m_actors(aActors)
{
    Listen<CopyHeldEvent, &GuestCopies::OnCopyHeld>(m_connections, aDispatcher, this);
}

void GuestCopies::OnCopyHeld(const CopyHeldEvent& acEvent) noexcept
{
    auto* pHeld = m_world.try_get<HeldCopyComponent>(acEvent.Entity);
    if (!pHeld)
        return;
    // Persistent for the session: a cell's unload frees its other references, disabled or not (TESObjectCELL 19113).
    if (!m_world.all_of<KeptCopyComponent>(acEvent.Entity))
        m_world.emplace<KeptCopyComponent>(acEvent.Entity, acEvent.FormId, m_actors.SetActorPersistent(acEvent.FormId, true));
    // Disabled already, or its owner's was: it stays so after the session too.
    if (acEvent.OwnerDisabled || pHeld->Hidden || m_actors.IsActorDisabled(acEvent.FormId))
        return;
    spdlog::info("Copy {:X}: no owner: hidden", acEvent.FormId);
    m_actors.SetActorHidden(acEvent.FormId, true);
    pHeld->Hidden = true;
}
