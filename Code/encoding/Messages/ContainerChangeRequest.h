#pragma once

#include "Message.h"

#include <Structs/GameId.h>

// A guest's container changed (its player took or stored an item): a proposal, as the count it holds now.
struct ContainerChangeRequest final : RequestMessage
{
    static constexpr RequestOpcode Opcode = kContainerChangeRequest;

    ContainerChangeRequest()
        : RequestMessage(Opcode)
    {
    }

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    bool operator==(const ContainerChangeRequest& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && Container == acRhs.Container && Item == acRhs.Item && Count == acRhs.Count;
    }

    GameId Container{};
    GameId Item{};
    uint32_t Count{};
};
