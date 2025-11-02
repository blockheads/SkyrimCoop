#pragma once

namespace Server
{

/**
 * @brief Dispatched whenever a (player) character has been spawned.
 */
struct CharacterSpawnedEvent
{
    CharacterSpawnedEvent(const entt::entity aEntity)
        : Entity{aEntity}
    {
    }

    entt::entity Entity;
};

} // namespace Server
