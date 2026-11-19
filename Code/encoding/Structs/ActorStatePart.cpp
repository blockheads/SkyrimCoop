#include <Structs/ActorStatePart.h>
#include <Wire/Serialization.h>

using Wire::Serialization;

bool ActorStatePart::SameState(const ActorStatePart& acRhs) const noexcept
{
    return Flags1 == acRhs.Flags1 && Flags2 == acRhs.Flags2 && InKillMove == acRhs.InKillMove && Killer == acRhs.Killer && HandsUp == acRhs.HandsUp;
}

void ActorStatePart::Serialize(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Tick);
    Serialization::WriteVarInt(aWriter, Flags1);
    Serialization::WriteVarInt(aWriter, Flags2);
    Serialization::WriteBool(aWriter, InKillMove);
    Serialization::WriteVarInt(aWriter, Killer);
    Serialization::WriteBool(aWriter, HandsUp);
}

void ActorStatePart::Deserialize(Wire::Buffer::Reader& aReader) noexcept
{
    Tick = Serialization::ReadVarInt(aReader);
    Flags1 = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Flags2 = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    InKillMove = Serialization::ReadBool(aReader);
    Killer = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    HandsUp = Serialization::ReadBool(aReader);
}
