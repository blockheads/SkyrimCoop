#pragma once

#include <NoCopyMove.h>

struct World;
class IActorBridge;

/**
 * @brief A copy with no owner goes (docs/re/systems/copy-lifecycle.md): the host's game decides what is loaded, so a
 * held reference that has 3D on this game, when its owner dropped it or when this game's loaded cells reach it later, is
 * hidden until the owner spawns it again or the session ends (CharacterService).
 */
struct GuestCopies
{
    GuestCopies(World& aWorld, IActorBridge& aActors) noexcept;

    NO_COPY_MOVE(GuestCopies);

    void Update() noexcept;

private:
    World& m_world;
    IActorBridge& m_actors;
};
