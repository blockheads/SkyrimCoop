#include <Messages/ContainerChangeRequest.h>

#include <Wire/Serialization.h>

void ContainerChangeRequest::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Container.Serialize(aWriter);
    Item.Serialize(aWriter);
    Serialization::WriteVarInt(aWriter, Count);
}

void ContainerChangeRequest::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    RequestMessage::DeserializeRaw(aReader);

    Container.Deserialize(aReader);
    Item.Deserialize(aReader);
    Count = static_cast<uint32_t>(Serialization::ReadVarInt(aReader));
}
