#include <Services/Guest/GuestCopies.h>

#include <Bridges/IActorBridge.h>
#include <Components.h>
#include <World.h>

GuestCopies::GuestCopies(World& aWorld, IActorBridge& aActors) noexcept
    : m_world(aWorld)
    , m_actors(aActors)
{
}

void GuestCopies::Update() noexcept
{
    // With a RemoteComponent it has its owner again (a dismounted horse before the host takes it).
    for (auto&& [cEntity, held, cForm] : m_world.view<HeldCopyComponent, FormIdComponent>(entt::exclude<RemoteComponent>).each())
        if (m_actors.ActorHas3D(cForm.Id))
        {
            spdlog::info("Copy {:X}: no owner and in this game's area: hidden", cForm.Id);
            m_actors.DisableActor(cForm.Id, false);
            held.Hidden = true;
        }
}
