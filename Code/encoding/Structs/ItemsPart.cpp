#include <Structs/ItemsPart.h>
#include <Wire/Serialization.h>

#include <algorithm>

using Wire::Serialization;

namespace
{
std::vector<Inventory::Entry>::iterator FindSame(std::vector<Inventory::Entry>& aStacks, const Inventory::Entry& acStack)
{
    return std::ranges::find_if(aStacks, [&](const Inventory::Entry& acHeld) { return ItemsPart::SameStack(acHeld, acStack); });
}

// acStack's count added to its like in aStacks, or acStack appended.
void Add(std::vector<Inventory::Entry>& aStacks, const Inventory::Entry& acStack)
{
    if (const auto it = FindSame(aStacks, acStack); it != aStacks.end())
        it->Count += acStack.Count;
    else
        aStacks.push_back(acStack);
}
} // namespace

bool ItemsPart::SameStack(const Inventory::Entry& acLhs, Inventory::Entry aRhs) noexcept
{
    aRhs.ExtraCharge = acLhs.ExtraCharge;
    return acLhs.CanBeMerged(aRhs);
}

ItemsPart ItemsPart::Of(const Inventory& acNow) noexcept
{
    ItemsPart part;
    part.Full = true;
    part.Items.CurrentMagicEquipment = acNow.CurrentMagicEquipment;
    for (const auto& cStack : acNow.Entries)
        Add(part.Items.Entries, cStack);
    std::erase_if(part.Items.Entries, [](const Inventory::Entry& acStack) { return acStack.Count <= 0; });
    return part;
}

std::optional<ItemsPart> ItemsPart::Change(const ItemsPart& acHeld, const Inventory& acNow) noexcept
{
    const ItemsPart cNow = Of(acNow);
    ItemsPart change;
    change.Items.CurrentMagicEquipment = cNow.Items.CurrentMagicEquipment;
    auto held = acHeld.Items.Entries;
    for (const auto& cStack : cNow.Items.Entries)
    {
        const auto it = FindSame(held, cStack);
        if (it == held.end() || it->Count != cStack.Count)
            change.Items.Entries.push_back(cStack);
        if (it != held.end())
            held.erase(it);
    }
    for (Inventory::Entry gone : held)
    {
        gone.Count = 0;
        change.Items.Entries.push_back(gone);
    }
    if (change.Items.Entries.empty() && change.Items.CurrentMagicEquipment == acHeld.Items.CurrentMagicEquipment)
        return std::nullopt;
    return change;
}

ItemsPart ItemsPart::Merged(const ItemsPart& acHeld, const ItemsPart& acChange) noexcept
{
    if (acChange.Full)
        return acChange;
    ItemsPart merged = acHeld;
    merged.Full = true;
    merged.Items.CurrentMagicEquipment = acChange.Items.CurrentMagicEquipment;
    auto& stacks = merged.Items.Entries;
    for (const auto& cStack : acChange.Items.Entries)
    {
        const auto it = FindSame(stacks, cStack);
        if (it != stacks.end())
            it->Count = cStack.Count;
        else if (cStack.Count > 0)
            stacks.push_back(cStack);
    }
    std::erase_if(stacks, [](const Inventory::Entry& acStack) { return acStack.Count <= 0; });
    return merged;
}
