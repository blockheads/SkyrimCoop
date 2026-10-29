#pragma once
#include <Messages/Message.h>
#include <Structs/Inventory.h>
#include <Wire/Serialization.h>

// Positive Item.Count takes from the container; negative stores in it. Actors use their session id.
struct ContainerTransferRequest final : RequestMessage
{
    static constexpr RequestOpcode Opcode = kContainerTransferRequest;
    ContainerTransferRequest() : RequestMessage(Opcode)
    {
    }
    GameId Container{};
    uint32_t ContainerActorId{};
    Inventory::Entry Item;
    bool Steal{};
    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override
    {
        Container.Serialize(aWriter);
        Wire::Serialization::WriteVarInt(aWriter, ContainerActorId);
        Item.Serialize(aWriter);
        Wire::Serialization::WriteBool(aWriter, Steal);
    }
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override
    {
        Container.Deserialize(aReader);
        ContainerActorId = Wire::Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
        Item.Deserialize(aReader);
        Steal = Wire::Serialization::ReadBool(aReader);
    }
    bool operator==(const ContainerTransferRequest& acOther) const noexcept
    {
        return Container == acOther.Container && ContainerActorId == acOther.ContainerActorId && Item == acOther.Item &&
               Steal == acOther.Steal;
    }
};
