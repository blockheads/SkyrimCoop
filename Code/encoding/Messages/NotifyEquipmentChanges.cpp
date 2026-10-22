#include <Messages/NotifyEquipmentChanges.h>
#include <Wire/Serialization.h>

void NotifyEquipmentChanges::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, SessionId);
    ItemId.Serialize(aWriter);
    EquipSlotId.Serialize(aWriter);
    Serialization::WriteVarInt(aWriter, Count);
    Serialization::WriteBool(aWriter, Unequip);
    Serialization::WriteBool(aWriter, IsSpell);
    Serialization::WriteBool(aWriter, IsShout);
}

void NotifyEquipmentChanges::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    NotifyMessage::DeserializeRaw(aReader);

    SessionId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    ItemId.Deserialize(aReader);
    EquipSlotId.Deserialize(aReader);
    Count = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Unequip = Serialization::ReadBool(aReader);
    IsSpell = Serialization::ReadBool(aReader);
    IsShout = Serialization::ReadBool(aReader);
}
