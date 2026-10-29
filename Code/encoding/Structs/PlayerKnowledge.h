#pragma once

#include <Structs/GameId.h>
#include <cstdint>
#include <vector>

struct PlayerKnowledge
{
    struct Ingredient
    {
        GameId Id;
        uint16_t Effects{};
        uint16_t Uses{};
        bool operator==(const Ingredient&) const = default;
    };

    // Old character records have no snapshot; an empty new snapshot means nothing learned.
    bool Present{};
    std::vector<Ingredient> Ingredients;
    std::vector<GameId> Enchantments;
    bool operator==(const PlayerKnowledge&) const = default;
};
