#pragma once

#include <Events/Listen.h>
#include <NoCopyMove.h>

struct World;
struct CopyHeldEvent;
class IActorBridge;

/**
 * @brief A copy with no owner goes (docs/design/copy-contract.md, "A copy's lifecycle"): kept and hidden as its owner's
 * actor goes, until the owner spawns it or the session ends (CharacterService).
 */
struct GuestCopies
{
    GuestCopies(World& aWorld, entt::dispatcher& aDispatcher, IActorBridge& aActors) noexcept;

    NO_COPY_MOVE(GuestCopies);

private:
    void OnCopyHeld(const CopyHeldEvent& acEvent) noexcept;

    World& m_world;
    IActorBridge& m_actors;

    std::vector<entt::scoped_connection> m_connections;
};
