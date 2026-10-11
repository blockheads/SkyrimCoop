#include <Messages/RequestHealthChangeBroadcast.h>

void RequestHealthChangeBroadcast::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Id);
    Serialization::WriteFloat(aWriter, DeltaHealth);
    Serialization::WriteBool(aWriter, HasAttacker);
    Serialization::WriteVarInt(aWriter, AttackerId);
}

void RequestHealthChangeBroadcast::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    Id = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    DeltaHealth = Serialization::ReadFloat(aReader);
    HasAttacker = Serialization::ReadBool(aReader);
    AttackerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
}
