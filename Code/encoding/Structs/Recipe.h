#pragma once

#include <Structs/GameId.h>
#include <Structs/Inventory.h>
#include <Wire/Buffer.h>

#include <vector>

// A player-made potion or poison (BGSCreatedObjectManager's AddPotion 36167, AddPoison 36168). Each game makes its own
// form for it, so the session names it by its recipe (docs/design/crafted-potions.md).
struct Recipe
{
    // A recipe id's ModId, no plugin's (ToGame gives 0); its BaseId is a hash of the recipe.
    static constexpr uint32_t kModId = 0xFFFFFFFE;

    bool IsPoison{};
    std::vector<Inventory::EffectItem> Effects; // in its form's order

    // The same potion: poison or not, the same effects in any order (an effect's cost is each game's own).
    bool operator==(const Recipe& acRhs) const noexcept;

    // Its id in the session, the same in every game.
    [[nodiscard]] GameId Id() const noexcept;
    [[nodiscard]] static bool IsId(const GameId& acId) noexcept { return acId.ModId == kModId; }

    void Serialize(Wire::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(Wire::Buffer::Reader& aReader) noexcept;
};
