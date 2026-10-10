#include <Messages/NotifyEquipmentSnapshot.h>
#include <TiltedCore/Serialization.hpp>

void NotifyEquipmentSnapshot::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, ServerId);
    Equipment.Serialize(aWriter);
}

void NotifyEquipmentSnapshot::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    ServerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Equipment.Deserialize(aReader);
}
