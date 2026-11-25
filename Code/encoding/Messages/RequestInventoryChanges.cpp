#include <Messages/RequestInventoryChanges.h>
#include <Wire/Serialization.h>

void RequestInventoryChanges::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, SessionId);
    Item.Serialize(aWriter);
    Serialization::WriteBool(aWriter, Drop);
    Serialization::WriteBool(aWriter, UpdateClients);
    Serialization::WriteBool(aWriter, NewPartyItem);
    Serialization::WriteBool(aWriter, StoryGiven);
    Serialization::WriteBool(aWriter, AliasHeld);
    Serialization::WriteBool(aWriter, Wear);
}

void RequestInventoryChanges::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    RequestMessage::DeserializeRaw(aReader);

    SessionId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Item.Deserialize(aReader);
    Drop = Serialization::ReadBool(aReader);
    UpdateClients = Serialization::ReadBool(aReader);
    NewPartyItem = Serialization::ReadBool(aReader);
    StoryGiven = Serialization::ReadBool(aReader);
    AliasHeld = Serialization::ReadBool(aReader);
    Wear = Serialization::ReadBool(aReader);
}
