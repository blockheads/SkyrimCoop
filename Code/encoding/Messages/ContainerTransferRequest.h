#pragma once
#include <Messages/Message.h>
#include <Structs/Inventory.h>
#include <Wire/Serialization.h>

// Positive Item.Count takes from the container; negative stores in it. Actors use their session id.
struct ContainerTransferRequest final : MessageOf<ContainerTransferRequest, kContainerTransferRequest>
{
    GameId Container{};
    uint32_t ContainerActorId{};
    Inventory::Entry Item;
    bool Steal{};
    template <class TArchive> void Fields(TArchive& aArchive) noexcept
    {
        aArchive(Container, ContainerActorId, Item, Steal);
    }
    bool operator==(const ContainerTransferRequest&) const noexcept = default;
};
