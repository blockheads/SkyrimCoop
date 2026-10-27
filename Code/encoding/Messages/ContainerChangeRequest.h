#pragma once

#include "Message.h"

#include <Structs/GameId.h>
#include <Structs/Inventory.h>

// A guest's container changed (its player took or stored an item): a proposal, as the count it holds now and the
// stacks of them with extra data (an enchantment, tempering...).
struct ContainerChangeRequest final : RequestMessage
{
    static constexpr RequestOpcode Opcode = kContainerChangeRequest;
    static constexpr size_t kMaxStacks = 4096;

    ContainerChangeRequest()
        : RequestMessage(Opcode)
    {
    }

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    bool operator==(const ContainerChangeRequest& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && Container == acRhs.Container && Item == acRhs.Item && Count == acRhs.Count &&
               Stacks == acRhs.Stacks;
    }

    GameId Container{};
    GameId Item{};
    uint32_t Count{};
    std::vector<Inventory::Entry> Stacks;
};
