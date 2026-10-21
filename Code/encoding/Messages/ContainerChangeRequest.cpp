#include <Messages/ContainerChangeRequest.h>

#include <TiltedCore/Serialization.hpp>

void ContainerChangeRequest::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Container.Serialize(aWriter);
    Item.Serialize(aWriter);
    Serialization::WriteVarInt(aWriter, Count);
}

void ContainerChangeRequest::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    Container.Deserialize(aReader);
    Item.Deserialize(aReader);
    Count = static_cast<uint32_t>(Serialization::ReadVarInt(aReader));
}
