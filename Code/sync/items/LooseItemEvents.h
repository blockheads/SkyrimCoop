#pragma once

#include <cstdint>

/**
 * @brief This game's player dropped an item: the engine made ObjectId, a loose item in the world
 * (PlayerCharacter::DropObject 40454).
 */
struct ItemDroppedEvent
{
    uint32_t ObjectId{};
};

/**
 * @brief This game's player tried to pick up the party's loose item ObjectId; the engine did not, the host's game
 * decides (a guest's PlayerCharacter::PickUpObject 40533).
 */
struct ItemPickUpEvent
{
    uint32_t ObjectId{};
};
