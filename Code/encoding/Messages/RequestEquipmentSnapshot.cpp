#include <Messages/RequestEquipmentSnapshot.h>
#include <TiltedCore/Serialization.hpp>

void RequestEquipmentSnapshot::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, ServerId);
    Equipment.Serialize(aWriter);
}

void RequestEquipmentSnapshot::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    ServerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Equipment.Deserialize(aReader);
}
