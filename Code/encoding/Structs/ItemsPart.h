#pragma once

#include <Wire/Archive.h>

#include <Structs/Inventory.h>

#include <optional>

// An actor's items as its owner declares them (docs/design/one-simulator.md, piece 3): its stacks by count, worn flags
// and all, and its hands' spells. Full: every stack (a spawn, an ask); else only the stacks that changed, each at its
// new count (0: gone).
struct ItemsPart
{
    bool operator==(const ItemsPart&) const noexcept = default;
    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(Full, Items); }

    void Serialize(Wire::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(Wire::Buffer::Reader& aReader) noexcept;

    // One stack, whatever its charge: an enchanted weapon's charge falling with each hit is no change of items.
    [[nodiscard]] static bool SameStack(const Inventory::Entry& acLhs, Inventory::Entry aRhs) noexcept;
    // Every stack of acNow, as a full part (stacks alike summed).
    [[nodiscard]] static ItemsPart Of(const Inventory& acNow) noexcept;
    // What changed from acHeld (a full part) to acNow; none: nothing.
    [[nodiscard]] static std::optional<ItemsPart> Change(const ItemsPart& acHeld, const Inventory& acNow) noexcept;
    // acHeld (full) with acChange applied: a full part.
    [[nodiscard]] static ItemsPart Merged(const ItemsPart& acHeld, const ItemsPart& acChange) noexcept;

    bool Full{};
    Inventory Items;
};
