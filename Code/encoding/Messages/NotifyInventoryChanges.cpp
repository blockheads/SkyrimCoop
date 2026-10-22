#include <Messages/NotifyInventoryChanges.h>
#include <Wire/Serialization.h>

void NotifyInventoryChanges::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, SessionId);
    Item.Serialize(aWriter);
    Serialization::WriteBool(aWriter, Drop);
}

void NotifyInventoryChanges::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    NotifyMessage::DeserializeRaw(aReader);

    SessionId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Item.Deserialize(aReader);
    Drop = Serialization::ReadBool(aReader);
}
