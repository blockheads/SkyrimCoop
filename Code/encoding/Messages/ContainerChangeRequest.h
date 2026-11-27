#pragma once

#include "Message.h"

#include <Structs/GameId.h>
#include <Structs/Inventory.h>

// A guest's container changed (its player took or stored an item): a proposal, as the count it holds now and the
// stacks of them with extra data (an enchantment, tempering...).
struct ContainerChangeRequest final : MessageOf<ContainerChangeRequest, kContainerChangeRequest>
{
    static constexpr size_t kMaxStacks = 4096;


    template <class TArchive> void Fields(TArchive& aArchive) noexcept
    {
        aArchive(Container, Item, Count, Wire::List(Stacks, kMaxStacks));
    }
    bool operator==(const ContainerChangeRequest&) const noexcept = default;

    GameId Container{};
    GameId Item{};
    uint32_t Count{};
    std::vector<Inventory::Entry> Stacks;
};
